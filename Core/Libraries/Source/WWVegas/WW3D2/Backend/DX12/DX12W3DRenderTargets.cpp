/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2026 TheSuperHackers
** Distributed under the GNU General Public License, v3 or later.
*/
#include "DX12W3DRenderTargets.h"
#include "DX12PresentationDevice.h"
#include "DX12W3DTextures.h"

#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <new>
#include <string>
#include <unordered_set>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace
{
    const D3D12_RESOURCE_STATES Sampled =
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    D3D12_HEAP_PROPERTIES DefaultHeap()
    {
        D3D12_HEAP_PROPERTIES heap = {};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        return heap;
    }
}

struct DX12W3DRenderTargets::Impl
{
    enum class Kind { TextureColor, TextureDepth, PrimaryColor, PrimaryDepth, MSAAColor };
    struct Surface
    {
        std::atomic<unsigned> refs{1};
        Kind kind = Kind::TextureColor;
        RenderTextureHandle texture = nullptr;
        RenderSurfaceDescription description;
        DX12PresentationDevice * primaryOwner = nullptr;
        unsigned long long primarySerial = 0;
        unsigned samples = 1;
        ComPtr<ID3D12Device> viewDevice;
        ComPtr<ID3D12DescriptorHeap> viewHeap;
        // Keep the previous native generation alive while comparing descriptor
        // owners. A released texture could otherwise allocate a new COM object
        // at the same CPU address and incorrectly hit the cached RTV/DSV.
        ComPtr<ID3D12Resource> viewResource;
        ComPtr<ID3D12Resource> msaa;
        // The multisampled RTV is NOT the resolved R04 texture. Retained
        // W3D surfaces must rebuild this extra allocation on device reset.
        ComPtr<ID3D12Device> msaaDevice;
        D3D12_RESOURCE_STATES msaaState = D3D12_RESOURCE_STATE_COMMON;

        ~Surface() { DX12W3DTextures::Release(texture); }
        bool IsDepth() const { return kind == Kind::TextureDepth || kind == Kind::PrimaryDepth; }
        bool IsColor() const { return !IsDepth(); }
        bool IsPrimary() const { return kind == Kind::PrimaryColor || kind == Kind::PrimaryDepth; }
    };

    mutable std::mutex mutex;
    std::unordered_set<Surface *> owned;
    DX12PresentationDevice * owner = nullptr;
    unsigned long long serial = 0;
    Surface * activeColor = nullptr;
    Surface * activeDepth = nullptr;
    std::string error;

    bool Fail(const char * why) { error = why; return false; }
    RenderSurfaceHandle Register(Surface * surface)
    {
        if (!surface) { Fail("Allocating R07 GPU surface wrapper failed"); return nullptr; }
        std::lock_guard<std::mutex> guard(mutex);
        owned.insert(surface);
        return reinterpret_cast<RenderSurfaceHandle>(surface);
    }
    Surface * Lookup(RenderSurfaceHandle h) const
    {
        if (!h) return nullptr;
        auto * s = reinterpret_cast<Surface *>(h);
        std::lock_guard<std::mutex> guard(mutex);
        return owned.count(s) ? s : nullptr;
    }
    void Retain(Surface * surface)
    {
        if (surface) surface->refs.fetch_add(1, std::memory_order_relaxed);
    }
    void Release(Surface * surface)
    {
        if (!surface) return;
        bool dispose = false;
        {
            std::lock_guard<std::mutex> guard(mutex);
            if (!owned.count(surface)) return;
            if (surface->refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                owned.erase(surface);
                dispose = true;
            }
        }
        if (dispose) delete surface;
    }
    void ClearActive()
    {
        Surface * color = std::exchange(activeColor, nullptr);
        Surface * depth = std::exchange(activeDepth, nullptr);
        Release(color);
        Release(depth);
    }
    bool ValidFrame(DX12PresentationDevice & f)
    {
        return owner == &f && f.IsRecording() && f.NativeDevice() &&
               serial == f.RecordingSerial() && f.ActiveCommandList();
    }
    bool ValidSurface(Surface * s, DX12PresentationDevice & f)
    {
        if (!s) return true;
        if (s->IsPrimary())
            return s->primaryOwner == &f &&
                s->primarySerial == f.RecordingSerial() &&
                (s->kind == Kind::PrimaryColor ? f.CurrentBackBuffer() :
                                                       f.CurrentDepthTarget()) != nullptr;
        return s->texture != nullptr && DX12W3DTextures::IsAttachment(s->texture);
    }

    ID3D12Resource * Resource(Surface * s, DX12PresentationDevice & f)
    {
        if (!s)
            return nullptr;
        if (s->kind == Kind::PrimaryColor) return f.CurrentBackBuffer();
        if (s->kind == Kind::PrimaryDepth) return f.CurrentDepthTarget();
        if (s->kind == Kind::MSAAColor) return s->msaa.Get();
        return DX12W3DTextures::NativeResource(s->texture);
    }

    bool PrepareView(Surface & s, DX12PresentationDevice & frame)
    {
        if (s.IsPrimary()) return true; // RTV/DSV belongs to the current swapchain frame
        if (s.kind == Kind::MSAAColor &&
            (!s.msaa || s.msaaDevice.Get() != frame.NativeDevice()))
        {
            // The resolved single-sample texture is rehydrated separately
            // by R04. The multisampled RTV must also belong to this device,
            // not to the D3D12 device that created the original W3D handle.
            D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
            quality.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            quality.SampleCount = s.samples;
            if (FAILED(frame.NativeDevice()->CheckFeatureSupport(
                    D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                    &quality, sizeof(quality))) || !quality.NumQualityLevels)
                return Fail("Retained MSAA surface is unsupported after device reset");
            D3D12_RESOURCE_DESC desc = {};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = s.description.width;
            desc.Height = s.description.height;
            desc.DepthOrArraySize = desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = s.samples;
            desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            D3D12_CLEAR_VALUE clear = {};
            clear.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            clear.Color[3] = 1.f;
            const auto heap = DefaultHeap();
            ComPtr<ID3D12Resource> replacement;
            if (FAILED(frame.NativeDevice()->CreateCommittedResource(
                    &heap, D3D12_HEAP_FLAG_NONE, &desc,
                    D3D12_RESOURCE_STATE_COMMON, &clear,
                    IID_PPV_ARGS(&replacement))))
                return Fail("Recreating retained MSAA RTV on the new D3D12 device failed");
            // Previously recorded commands pin their resources and heaps
            // until their frame fences. No old-device descriptor is reused.
            s.msaa = std::move(replacement);
            s.msaaDevice = frame.NativeDevice();
            s.msaaState = D3D12_RESOURCE_STATE_COMMON;
            s.viewHeap.Reset();
            s.viewResource.Reset();
            s.viewDevice.Reset();
        }
        if (s.kind != Kind::MSAAColor &&
            !DX12W3DTextures::EnsureAttachment(frame.NativeDevice(), s.texture))
            return Fail(DX12W3DTextures::LastError());
        ID3D12Resource * const native = Resource(&s, frame);
        if (!native) return Fail("GPU surface has no native texture allocation");
        if (s.viewHeap && s.viewDevice.Get() == frame.NativeDevice() &&
            s.viewResource.Get() == native)
            return true;

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = s.IsDepth() ? D3D12_DESCRIPTOR_HEAP_TYPE_DSV :
                                      D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = 1;
        ComPtr<ID3D12DescriptorHeap> heap;
        if (FAILED(frame.NativeDevice()->CreateDescriptorHeap(
                &heapDesc, IID_PPV_ARGS(&heap))))
            return Fail("Creating R07 native RTV/DSV descriptor failed");
        const auto handle = heap->GetCPUDescriptorHandleForHeapStart();
        if (s.IsDepth())
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dsv = {};
            dsv.Format = DX12W3DTextures::AttachmentViewFormat(s.texture);
            dsv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            frame.NativeDevice()->CreateDepthStencilView(native, &dsv, handle);
        }
        else
        {
            D3D12_RENDER_TARGET_VIEW_DESC rtv = {};
            rtv.Format = s.kind == Kind::MSAAColor ? DXGI_FORMAT_B8G8R8A8_UNORM :
                DX12W3DTextures::AttachmentViewFormat(s.texture);
            rtv.ViewDimension = s.kind == Kind::MSAAColor ?
                D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
            frame.NativeDevice()->CreateRenderTargetView(native, &rtv, handle);
        }
        s.viewHeap = std::move(heap);
        s.viewDevice = frame.NativeDevice();
        s.viewResource = native;
        return true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE View(Surface * s, DX12PresentationDevice & frame,
                                     bool depth) const
    {
        if (!s) return depth ? frame.CurrentDepthDSV() : frame.CurrentBackBufferRTV();
        if (s->IsPrimary())
            return depth ? frame.CurrentDepthDSV() : frame.CurrentBackBufferRTV();
        return s->viewHeap->GetCPUDescriptorHandleForHeapStart();
    }

    bool Transition(Surface & surface, DX12PresentationDevice & frame,
                    D3D12_RESOURCE_STATES desired)
    {
        if (surface.IsPrimary())
            return frame.TransitionFrameResource(Resource(&surface, frame), desired)
                || Fail(frame.LastError());
        if (surface.kind != Kind::MSAAColor)
            return DX12W3DTextures::TransitionAttachment(frame, surface.texture, desired)
                || Fail(DX12W3DTextures::LastError());

        D3D12_RESOURCE_STATES tracked = D3D12_RESOURCE_STATE_COMMON;
        if (!frame.GetFrameResourceState(surface.msaa.Get(), tracked) &&
            !frame.RegisterFrameResource(surface.msaa.Get(), surface.msaaState))
            return Fail(frame.LastError());
        if (!frame.TransitionFrameResource(surface.msaa.Get(), desired) ||
            !frame.RetainFrameResource(surface.msaa.Get()))
            return Fail(frame.LastError());
        surface.msaaState = desired;
        return true;
    }

    bool FinishBound(DX12PresentationDevice & frame)
    {
        if (activeColor && activeColor->kind == Kind::MSAAColor)
        {
            Surface & multi = *activeColor;
            if (!Transition(multi, frame, D3D12_RESOURCE_STATE_RESOLVE_SOURCE) ||
                !DX12W3DTextures::TransitionAttachment(frame, multi.texture,
                    D3D12_RESOURCE_STATE_RESOLVE_DEST))
                return Fail("Preparing MSAA ResolveSubresource state failed");
            ID3D12Resource * single = DX12W3DTextures::NativeResource(multi.texture);
            frame.ActiveCommandList()->ResolveSubresource(
                single, 0, multi.msaa.Get(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);
            if (!DX12W3DTextures::TransitionAttachment(frame, multi.texture, Sampled))
                return Fail(DX12W3DTextures::LastError());
        }
        else if (activeColor && !activeColor->IsPrimary() &&
                 !Transition(*activeColor, frame, Sampled))
            return false;
        if (activeDepth && !activeDepth->IsPrimary() &&
            !Transition(*activeDepth, frame, Sampled))
            return false;
        return true;
    }
};

DX12W3DRenderTargets::DX12W3DRenderTargets() : impl_(new Impl) {}
DX12W3DRenderTargets::~DX12W3DRenderTargets()
{
    Reset();
    // All remaining callers should release their acquired surfaces before
    // shutdown. At process exit, dispose any orphaned handles.
    for (auto * s : impl_->owned) delete s;
    impl_->owned.clear();
}

RenderTexture2DHandle DX12W3DRenderTargets::CreateOffscreenTexture(
    DX12PresentationDevice & frame, const RenderSurfaceDescription & desc)
{
    impl_->error.clear();
    if (!frame.IsReady() || !desc.Is_Valid() || desc.multisampled)
    {
        impl_->Fail("Offscreen RTT requires valid dimensions and single-sample image");
        return nullptr;
    }
    RenderTexture2DDescription texture;
    texture.width = desc.width;
    texture.height = desc.height;
    texture.pixelFormat = desc.pixelFormat;
    texture.mipLevels = 1;
    texture.memory = RENDER_TEXTURE_DEFAULT;
    texture.renderTarget = true;
    return DX12W3DTextures::Create2D(frame.NativeDevice(), texture);
}

RenderTexture2DHandle DX12W3DRenderTargets::CreateDepthTexture(
    DX12PresentationDevice & frame, const RenderTexture2DDescription & desc)
{
    impl_->error.clear();
    if (!frame.IsReady() || !desc.Is_Valid())
    {
        impl_->Fail("Cannot create R07 depth attachment without DX12 device");
        return nullptr;
    }
    return DX12W3DTextures::CreateDepth2D(frame.NativeDevice(), desc);
}

RenderSurfaceHandle DX12W3DRenderTargets::AcquireTextureLevel(
    DX12PresentationDevice & frame, RenderTexture2DHandle texture, unsigned mip)
{
    impl_->error.clear();
    const auto handle = reinterpret_cast<RenderTextureHandle>(texture);
    if (!frame.IsReady() || !handle || !DX12W3DTextures::IsAttachment(handle) || mip != 0 ||
        !DX12W3DTextures::EnsureAttachment(frame.NativeDevice(), handle))
    {
        impl_->Fail("Surface view requires a live native one-mip RTV/DSV texture");
        return nullptr;
    }
    RenderTextureLevelDescription level;
    if (!DX12W3DTextures::Describe(handle, mip, level))
        return nullptr;
    auto * s = new (std::nothrow) Impl::Surface;
    if (!s) return nullptr;
    s->kind = DX12W3DTextures::IsDepthAttachment(handle)
        ? Impl::Kind::TextureDepth : Impl::Kind::TextureColor;
    s->texture = handle;
    DX12W3DTextures::Retain(handle);
    s->description.width = level.width;
    s->description.height = level.height;
    s->description.pixelFormat = level.pixelFormat;
    if (!impl_->PrepareView(*s, frame))
    {
        delete s;
        return nullptr;
    }
    return impl_->Register(s);
}

RenderSurfaceHandle DX12W3DRenderTargets::AcquirePrimaryColor(
    DX12PresentationDevice & frame)
{
    impl_->error.clear();
    if (!frame.IsRecording() || !frame.CurrentBackBuffer()) return nullptr;
    auto * s = new (std::nothrow) Impl::Surface;
    if (!s) return nullptr;
    s->kind = Impl::Kind::PrimaryColor;
    s->primaryOwner = &frame;
    s->primarySerial = frame.RecordingSerial();
    s->description.width = frame.Width();
    s->description.height = frame.Height();
    s->description.pixelFormat = 2; // BGRA8
    return impl_->Register(s);
}
RenderSurfaceHandle DX12W3DRenderTargets::AcquirePrimaryDepth(
    DX12PresentationDevice & frame)
{
    impl_->error.clear();
    if (!frame.IsRecording() || !frame.CurrentDepthTarget()) return nullptr;
    auto * s = new (std::nothrow) Impl::Surface;
    if (!s) return nullptr;
    s->kind = Impl::Kind::PrimaryDepth;
    s->primaryOwner = &frame;
    s->primarySerial = frame.RecordingSerial();
    s->description.width = frame.Width();
    s->description.height = frame.Height();
    s->description.pixelFormat = 2; // WW3D_ZFORMAT_D32
    return impl_->Register(s);
}
RenderSurfaceHandle DX12W3DRenderTargets::AcquireCurrentColor(
    DX12PresentationDevice & frame)
{
    if (impl_->activeColor)
    {
        impl_->Retain(impl_->activeColor);
        return reinterpret_cast<RenderSurfaceHandle>(impl_->activeColor);
    }
    // A depth-only shadow pass has no RTV. Never report the unrelated
    // primary backbuffer as its active color attachment.
    return impl_->activeDepth ? nullptr : AcquirePrimaryColor(frame);
}
RenderSurfaceHandle DX12W3DRenderTargets::AcquireCurrentDepth(
    DX12PresentationDevice & frame)
{
    if (impl_->activeDepth)
    {
        impl_->Retain(impl_->activeDepth);
        return reinterpret_cast<RenderSurfaceHandle>(impl_->activeDepth);
    }
    // Default binding uses the swapchain depth surface.
    return impl_->activeColor ? nullptr : AcquirePrimaryDepth(frame);
}

RenderSurfaceHandle DX12W3DRenderTargets::CreateMSAAColor(
    DX12PresentationDevice & frame, unsigned width, unsigned height,
    unsigned format, unsigned samples)
{
    impl_->error.clear();
    if (!frame.IsReady() || !width || !height || width > 16384 || height > 16384 ||
        (format != 2 && format != 3) || (samples != 2 && samples != 4 && samples != 8))
    {
        impl_->Fail("Unsupported multisample render-target dimensions, format or count");
        return nullptr;
    }
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality = {};
    quality.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    quality.SampleCount = samples;
    quality.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    if (FAILED(frame.NativeDevice()->CheckFeatureSupport(
            D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &quality, sizeof(quality))) ||
        quality.NumQualityLevels == 0)
    {
        impl_->Fail("Requested native MSAA mode is unsupported on this device");
        return nullptr;
    }
    RenderSurfaceDescription resolveDesc;
    resolveDesc.width = width; resolveDesc.height = height; resolveDesc.pixelFormat = format;
    auto texture = CreateOffscreenTexture(frame, resolveDesc);
    if (!texture) return nullptr;
    const D3D12_HEAP_PROPERTIES heap = DefaultHeap();
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width; desc.Height = height;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = samples;
    desc.SampleDesc.Quality = 0;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    clear.Color[3] = 1.f;
    auto * s = new (std::nothrow) Impl::Surface;
    if (!s) { DX12W3DTextures::Release(reinterpret_cast<RenderTextureHandle>(texture)); return nullptr; }
    s->kind = Impl::Kind::MSAAColor;
    s->texture = reinterpret_cast<RenderTextureHandle>(texture); // owns initial reference
    s->samples = samples;
    s->description = resolveDesc;
    s->description.multisampled = true;
    if (FAILED(frame.NativeDevice()->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
            &clear, IID_PPV_ARGS(&s->msaa))))
    {
        delete s;
        impl_->Fail("Creating native MSAA color attachment failed");
        return nullptr;
    }
    s->msaaDevice = frame.NativeDevice();
    if (!impl_->PrepareView(*s, frame))
    {
        delete s;
        return nullptr;
    }
    return impl_->Register(s);
}
RenderTexture2DHandle DX12W3DRenderTargets::ResolvedTexture(RenderSurfaceHandle h) const
{
    auto * s = impl_->Lookup(h);
    return s && s->kind == Impl::Kind::MSAAColor
        ? reinterpret_cast<RenderTexture2DHandle>(s->texture) : nullptr;
}

bool DX12W3DRenderTargets::Owns(RenderSurfaceHandle surface) const
{
    return impl_->Lookup(surface) != nullptr;
}
void DX12W3DRenderTargets::Retain(RenderSurfaceHandle surface)
{
    impl_->Retain(impl_->Lookup(surface));
}
void DX12W3DRenderTargets::Release(RenderSurfaceHandle surface)
{
    impl_->Release(impl_->Lookup(surface));
}
bool DX12W3DRenderTargets::Describe(RenderSurfaceHandle h, RenderSurfaceDescription & out) const
{
    out = RenderSurfaceDescription();
    auto * s = impl_->Lookup(h);
    if (!s) return false;
    out = s->description;
    return true;
}

bool DX12W3DRenderTargets::BeginFrame(DX12PresentationDevice & frame)
{
    impl_->error.clear();
    if (!frame.IsRecording() || !frame.CurrentBackBuffer() || !frame.CurrentDepthTarget())
        return impl_->Fail("R07 BeginFrame requires native color/depth recording");
    Reset();
    impl_->owner = &frame;
    impl_->serial = frame.RecordingSerial();
    return true;
}
bool DX12W3DRenderTargets::Bind(DX12PresentationDevice & frame,
                                 RenderSurfaceHandle color, RenderSurfaceHandle depth)
{
    impl_->error.clear();
    if (!impl_->ValidFrame(frame)) return impl_->Fail("R07 target switching requires active frame");
    auto * c = impl_->Lookup(color);
    auto * d = impl_->Lookup(depth);
    if ((color && !c) || (depth && !d) ||
        (c && !c->IsColor()) || (d && !d->IsDepth()) ||
        !impl_->ValidSurface(c, frame) || !impl_->ValidSurface(d, frame))
        return impl_->Fail("R07 target handles must be live GPU color/depth surfaces");
    if (c == impl_->activeColor && d == impl_->activeDepth)
        return true;
    // nullptr/nullptr restores the primary RTV+DSV; nullptr/depth is a
    // legitimate depth-only shadow pass (zero color descriptors).
    const unsigned width = c ? c->description.width :
        d ? d->description.width : frame.Width();
    const unsigned height = c ? c->description.height :
        d ? d->description.height : frame.Height();
    const unsigned samples = c ? c->samples : d ? d->samples : 1u;
    if (d && (d->description.width != width || d->description.height != height ||
              d->samples != samples))
        return impl_->Fail("Color/depth attachment dimensions or MSAA counts disagree");
    if ((c && !impl_->PrepareView(*c, frame)) ||
        (d && !impl_->PrepareView(*d, frame)))
        return false;
    if (!impl_->FinishBound(frame))
        return false;

    if (c && !impl_->Transition(*c, frame, D3D12_RESOURCE_STATE_RENDER_TARGET))
        return false;
    if (!c && !d && !frame.TransitionFrameResource(
            frame.CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET))
        return impl_->Fail(frame.LastError());
    if (d && !impl_->Transition(*d, frame, D3D12_RESOURCE_STATE_DEPTH_WRITE))
        return false;
    if (!d && !c && !frame.TransitionFrameResource(
            frame.CurrentDepthTarget(), D3D12_RESOURCE_STATE_DEPTH_WRITE))
        return impl_->Fail(frame.LastError());

    const bool hasColor = c || !d;
    const bool hasDepth = d || !c;
    const D3D12_CPU_DESCRIPTOR_HANDLE rtv = hasColor
        ? impl_->View(c, frame, false) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv = hasDepth
        ? impl_->View(d, frame, true) : D3D12_CPU_DESCRIPTOR_HANDLE{};
    if ((hasColor && !rtv.ptr) || (hasDepth && !dsv.ptr))
        return impl_->Fail("R07 native output descriptors are unavailable");
    if ((c && c->viewHeap && !frame.RetainFrameObject(c->viewHeap.Get())) ||
        (d && d->viewHeap && !frame.RetainFrameObject(d->viewHeap.Get())))
        return impl_->Fail(frame.LastError());
    frame.ActiveCommandList()->OMSetRenderTargets(
        hasColor ? 1u : 0u, hasColor ? &rtv : nullptr, FALSE,
        hasDepth ? &dsv : nullptr);
    D3D12_VIEWPORT viewport = {0.f, 0.f,
        static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    frame.ActiveCommandList()->RSSetViewports(1, &viewport);
    frame.ActiveCommandList()->RSSetScissorRects(1, &scissor);
    impl_->Retain(c);
    impl_->Retain(d);
    impl_->ClearActive();
    impl_->activeColor = c;
    impl_->activeDepth = d;
    return true;
}

bool DX12W3DRenderTargets::Clear(DX12PresentationDevice & frame, bool color, bool depth,
                                  const float rgba[4], float z, unsigned stencil)
{
    impl_->error.clear();
    if (!impl_->ValidFrame(frame) || (color && !rgba) ||
        (depth && (z < 0.f || z > 1.f)))
        return impl_->Fail("R07 clear requires active frame and valid color/depth");
    if (color)
    {
        if (impl_->activeDepth && !impl_->activeColor)
            return impl_->Fail("Cannot clear a color target in a depth-only pass");
        const auto rtv = impl_->View(impl_->activeColor, frame, false);
        if (!rtv.ptr) return impl_->Fail("No RTV is bound for clear");
        frame.ActiveCommandList()->ClearRenderTargetView(rtv, rgba, 0, nullptr);
    }
    if (depth)
    {
        if (impl_->activeColor && !impl_->activeDepth)
            return impl_->Fail("No depth surface is bound for clear");
        auto dsv = impl_->View(impl_->activeDepth, frame, true);
        if (!dsv.ptr) return impl_->Fail("No DSV is bound for clear");
        const DXGI_FORMAT format = impl_->activeDepth && !impl_->activeDepth->IsPrimary()
            ? DX12W3DTextures::AttachmentViewFormat(impl_->activeDepth->texture)
            : DXGI_FORMAT_D32_FLOAT;
        D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;
        if (format == DXGI_FORMAT_D24_UNORM_S8_UINT)
            flags = static_cast<D3D12_CLEAR_FLAGS>(flags | D3D12_CLEAR_FLAG_STENCIL);
        frame.ActiveCommandList()->ClearDepthStencilView(dsv, flags, z,
            static_cast<UINT8>(stencil), 0, nullptr);
    }
    return true;
}

bool DX12W3DRenderTargets::EndFrame(DX12PresentationDevice & frame)
{
    if (!impl_->ValidFrame(frame) || !impl_->FinishBound(frame))
        return impl_->Fail("Failed to finish R07 offscreen frame");
    // Return OM state to the primary HWND before B05 performs PRESENT.
    const auto rtv = frame.CurrentBackBufferRTV();
    const auto dsv = frame.CurrentDepthDSV();
    frame.ActiveCommandList()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    impl_->ClearActive();
    impl_->owner = nullptr;
    impl_->serial = 0;
    return true;
}
void DX12W3DRenderTargets::Reset()
{
    impl_->ClearActive();
    impl_->owner = nullptr;
    impl_->serial = 0;
    impl_->error.clear();
}
bool DX12W3DRenderTargets::IsPrimary() const
{
    return (!impl_->activeColor || impl_->activeColor->IsPrimary()) &&
           (!impl_->activeDepth || impl_->activeDepth->IsPrimary());
}
RenderTextureHandle DX12W3DRenderTargets::TextureForSurface(RenderSurfaceHandle h) const
{
    auto * s = impl_->Lookup(h);
    return s ? s->texture : nullptr;
}
RenderTextureHandle DX12W3DRenderTargets::BoundColorTexture() const
{
    return impl_->activeColor ? impl_->activeColor->texture : nullptr;
}
RenderTextureHandle DX12W3DRenderTargets::BoundDepthTexture() const
{
    return impl_->activeDepth ? impl_->activeDepth->texture : nullptr;
}
ID3D12Resource * DX12W3DRenderTargets::BoundColorResource(DX12PresentationDevice & frame) const
{
    if (impl_->activeColor)
        return impl_->Resource(impl_->activeColor, frame);
    return impl_->activeDepth ? nullptr : frame.CurrentBackBuffer();
}
ID3D12Resource * DX12W3DRenderTargets::BoundDepthResource(DX12PresentationDevice & frame) const
{
    if (impl_->activeDepth) return impl_->Resource(impl_->activeDepth, frame);
    return impl_->activeColor ? nullptr : frame.CurrentDepthTarget();
}

bool DX12W3DRenderTargets::TransferResource(
    DX12PresentationDevice & frame, RenderSurfaceHandle handle,
    bool destination, ID3D12Resource * & resource)
{
    resource = nullptr;
    impl_->error.clear();
    auto * surface = impl_->Lookup(handle);
    if (!frame.IsRecording() || !surface ||
        !impl_->ValidSurface(surface, frame) || !surface->IsColor() ||
        (destination && surface->kind == Impl::Kind::MSAAColor))
        return impl_->Fail("R08 requires live single-sample GPU color surface");
    if (surface->kind == Impl::Kind::MSAAColor)
    {
        // Active multisample target must be resolved before copy. A previous
        // R07 Bind/EndFrame already resolved inactive targets.
        if (surface == impl_->activeColor && !impl_->FinishBound(frame))
            return false;
        if (!DX12W3DTextures::EnsureAttachment(frame.NativeDevice(), surface->texture))
            return impl_->Fail(DX12W3DTextures::LastError());
        resource = DX12W3DTextures::NativeResource(surface->texture);
    }
    else
    {
        if (!impl_->PrepareView(*surface, frame))
            return false;
        resource = impl_->Resource(surface, frame);
    }
    if (!resource || resource->GetDesc().SampleDesc.Count != 1 ||
        (resource->GetDesc().Format != DXGI_FORMAT_B8G8R8A8_UNORM &&
         resource->GetDesc().Format != DXGI_FORMAT_R8G8B8A8_UNORM))
        return impl_->Fail("R08 transfer requires one-sample 32-bit color image");
    return true;
}
bool DX12W3DRenderTargets::TransitionTransfer(
    DX12PresentationDevice & frame, RenderSurfaceHandle handle,
    D3D12_RESOURCE_STATES desired)
{
    auto * s = impl_->Lookup(handle);
    if (!frame.IsRecording() || !s || !impl_->ValidSurface(s, frame) ||
        !s->IsColor())
        return impl_->Fail("Invalid R08 transfer transition target");
    if (s->kind == Impl::Kind::MSAAColor)
        return DX12W3DTextures::TransitionAttachment(frame, s->texture, desired)
            || impl_->Fail(DX12W3DTextures::LastError());
    return impl_->Transition(*s, frame, desired);
}
bool DX12W3DRenderTargets::RebindAfterFlush(DX12PresentationDevice & frame)
{
    if (!impl_->ValidFrame(frame))
        return impl_->Fail("R08 command-list resume requires valid R07 frame");
    auto * c = impl_->activeColor;
    auto * d = impl_->activeDepth;
    if ((c && !impl_->PrepareView(*c, frame)) ||
        (d && !impl_->PrepareView(*d, frame)))
        return false;
    // An active MSAA color readback resolves the multisample RTV to its
    // single-sample texture. Re-enter the original render/depth states
    // before any more draw calls, not merely its OM descriptors.
    if ((c && !impl_->Transition(*c, frame, D3D12_RESOURCE_STATE_RENDER_TARGET)) ||
        (d && !impl_->Transition(*d, frame, D3D12_RESOURCE_STATE_DEPTH_WRITE)))
        return false;
    const bool color = c || !d;
    const bool depth = d || !c;
    const auto rtv = color ? impl_->View(c, frame, false) :
                       D3D12_CPU_DESCRIPTOR_HANDLE{};
    const auto dsv = depth ? impl_->View(d, frame, true) :
                       D3D12_CPU_DESCRIPTOR_HANDLE{};
    if ((color && !rtv.ptr) || (depth && !dsv.ptr))
        return impl_->Fail("R08 cannot restore native W3D output descriptors");
    frame.ActiveCommandList()->OMSetRenderTargets(
        color ? 1u : 0u, color ? &rtv : nullptr, FALSE,
        depth ? &dsv : nullptr);
    // Restored offscreen viewport; primary caller reapplies camera viewport.
    const unsigned width = c ? c->description.width :
        d ? d->description.width : frame.Width();
    const unsigned height = c ? c->description.height :
        d ? d->description.height : frame.Height();
    const D3D12_VIEWPORT viewport = {0, 0,
        static_cast<float>(width), static_cast<float>(height), 0, 1};
    const D3D12_RECT scissor = {0, 0,
        static_cast<LONG>(width), static_cast<LONG>(height)};
    frame.ActiveCommandList()->RSSetViewports(1, &viewport);
    frame.ActiveCommandList()->RSSetScissorRects(1, &scissor);
    return true;
}
unsigned DX12W3DRenderTargets::BoundSampleCount() const
{
    return impl_->activeColor ? impl_->activeColor->samples :
        impl_->activeDepth ? impl_->activeDepth->samples : 1u;
}
const char * DX12W3DRenderTargets::LastError() const
{
    return impl_->error.c_str();
}
