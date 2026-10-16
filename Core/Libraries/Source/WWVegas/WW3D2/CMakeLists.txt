# DX12-B02: select exactly one implementation of the static Renderer API.
# The default DX8 path and the opt-in DX12 gameplay mirror remain unchanged.
# DX12 includes B04 native frame diagnostics, but game rendering is non-playable until B07/B10.
set(W3D_RENDER_BACKEND "DX8" CACHE STRING "Authoritative W3D Renderer implementation (DX8 or DX12)")
set_property(CACHE W3D_RENDER_BACKEND PROPERTY STRINGS DX8 DX12)
if(NOT W3D_RENDER_BACKEND STREQUAL "DX8" AND
   NOT W3D_RENDER_BACKEND STREQUAL "DX12")
    message(FATAL_ERROR "W3D_RENDER_BACKEND must be DX8 or DX12 (got '${W3D_RENDER_BACKEND}')")
endif()
if(W3D_RENDER_BACKEND STREQUAL "DX12" AND
   (NOT WIN32 OR NOT MSVC OR IS_VS6_BUILD))
    message(FATAL_ERROR "DX12 Renderer bootstrap requires Windows with modern MSVC")
endif()

set(WW3D2_SRC
    aabtree.cpp
    aabtree.h
    #aabtreebuilder.cpp
    #aabtreebuilder.h
    agg_def.cpp
    agg_def.h
    animatedsoundmgr.cpp
    animatedsoundmgr.h
    #animobj.cpp
    #animobj.h
    #assetmgr.cpp
    #assetmgr.h
    assetstatus.cpp
    assetstatus.h
    bitmaphandler.cpp
    bitmaphandler.h
    bmp2d.cpp
    bmp2d.h
    #boxrobj.cpp
    #boxrobj.h
    bwrender.cpp
    bwrender.h
    #camera.cpp
    #camera.h
    #classid.h # unused
    collect.cpp
    collect.h
    colorspace.h
    coltest.cpp
    coltest.h
    coltype.h
    composite.cpp
    composite.h
    #dazzle.cpp
    #dazzle.h
    #ddsfile.cpp
    #ddsfile.h
    #decalmsh.cpp
    #decalmsh.h
    decalsys.cpp
    decalsys.h
    distlod.cpp
    distlod.h
    dllist.h
    dx8caps.cpp
    dx8caps.h
    dx8fvf.cpp
    dx8fvf.h
    dx8indexbuffer.cpp
    dx8indexbuffer.h
    dx8list.h
    dx8polygonrenderer.cpp
    dx8polygonrenderer.h
    dx8renderer.cpp
    dx8renderer.h
    dx8rendererdebugger.cpp
    dx8rendererdebugger.h
    dx8texman.cpp
    dx8texman.h
    dx8vertexbuffer.cpp
    dx8vertexbuffer.h
    dx8webbrowser.cpp
    dx8webbrowser.h
    dx8wrapper.cpp
    dx8wrapper.h
    dynamesh.cpp
    dynamesh.h
    font3d.cpp
    font3d.h
    formconv.cpp
    formconv.h
    FramGrab.cpp
    framgrab.h
    hanim.cpp
    hanim.h
    #hanimmgr.cpp
    #hanimmgr.h
    hcanim.cpp
    hcanim.h
    #hlod.cpp
    #hlod.h
    hmdldef.cpp
    hmdldef.h
    #hmorphanim.cpp
    #hmorphanim.h
    #hrawanim.cpp
    #hrawanim.h
    htree.cpp
    htree.h
    #htreemgr.cpp
    #htreemgr.h
    intersec.cpp
    intersec.h
    intersec.inl
    inttest.h
    layer.cpp
    layer.h
    #light.cpp
    #light.h
    #lightenvironment.cpp
    #lightenvironment.h
    line3d.cpp
    line3d.h
    #linegrp.cpp
    #linegrp.h
    #mapper.cpp
    #mapper.h
    matinfo.cpp
    matinfo.h
    matpass.cpp
    matpass.h
    #matrixmapper.cpp
    #matrixmapper.h
    #mesh.cpp
    #mesh.h
    #meshbuild.cpp
    #meshbuild.h
    meshdam.cpp
    meshdam.h
    #meshgeometry.cpp
    #meshgeometry.h
    #meshmatdesc.cpp
    #meshmatdesc.h
    #meshmdl.cpp
    #meshmdl.h
    #meshmdlio.cpp
    metalmap.cpp
    metalmap.h
    missingtexture.cpp
    missingtexture.h
    #motchan.cpp
    #motchan.h
    #nullrobj.cpp
    #nullrobj.h
    #part_buf.cpp
    #part_buf.h
    #part_emt.cpp
    #part_emt.h
    #part_ldr.cpp
    #part_ldr.h
    pivot.cpp
    pivot.h
    pointgr.cpp
    pointgr.h
    polyinfo.cpp
    polyinfo.h
    predlod.cpp
    predlod.h
    prim_anim.cpp
    prim_anim.h
    projector.cpp
    projector.h
    proto.cpp
    proto.h
    proxy.h
    rddesc.h
    #render2d.cpp
    #render2d.h
    render2dsentence.cpp
    render2dsentence.h
    Renderer.h
    renderobjectrecycler.cpp
    renderobjectrecycler.h
    rendobj.cpp
    rendobj.h
    #rinfo.cpp
    #rinfo.h
    ringobj.cpp
    ringobj.h
    robjlist.h
    #scene.cpp
    #scene.h
    segline.cpp
    segline.h
    seglinerenderer.cpp
    seglinerenderer.h
    #shader.cpp
    #shader.h
    shattersystem.cpp
    shattersystem.h
    shdlib.h
    snappts.cpp
    snapPts.h
    sortingrenderer.cpp
    sortingrenderer.h
    soundlibrarybridge.h
    soundrobj.cpp
    soundrobj.h
    sphereobj.cpp
    sphereobj.h
    static_sort_list.cpp
    static_sort_list.h
    statistics.cpp
    statistics.h
    streak.cpp
    streak.h
    streakRender.cpp
    streakRender.h
    stripoptimizer.cpp
    stripoptimizer.h
    surfaceclass.cpp
    surfaceclass.h
    texproject.cpp
    texproject.h
    #textdraw.cpp # unused
    textdraw.h
    texture.cpp
    texture.h
    texturefilter.cpp
    texturefilter.h
    textureloader.cpp
    textureloader.h
    texturethumbnail.cpp
    texturethumbnail.h
    #vertmaterial.cpp
    #vertmaterial.h
    visrasterizer.cpp
    visrasterizer.h
    w3d_dep.cpp
    w3d_dep.h
    #w3d_file.h
    w3d_obsolete.h
    w3d_util.cpp
    w3d_util.h
    w3derr.h
    w3dexclusionlist.cpp
    w3dexclusionlist.h
    ww3d.cpp
    ww3d.h
    ww3dformat.cpp
    ww3dformat.h
    ww3dids.h
    ww3dtrig.h
)

# The two backends define the same Renderer static API; never compile both.
if(W3D_RENDER_BACKEND STREQUAL "DX12")
    list(APPEND WW3D2_SRC Backend/DX12/DX12Backend.cpp)
else()
    list(APPEND WW3D2_SRC Backend/DX8Backend.cpp)
endif()

add_library(corei_ww3d2 INTERFACE)

target_sources(corei_ww3d2 INTERFACE ${WW3D2_SRC})
if(W3D_RENDER_BACKEND STREQUAL "DX12")
    target_compile_definitions(corei_ww3d2 INTERFACE W3D_RENDER_BACKEND_DX12=1)
else()
    target_compile_definitions(corei_ww3d2 INTERFACE W3D_RENDER_BACKEND_DX8=1)
endif()

target_include_directories(corei_ww3d2 INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

if (MSVC AND NOT IS_VS6_BUILD)
    target_link_libraries(corei_ww3d2 INTERFACE
        comsuppw
    )
endif()

target_link_libraries(corei_ww3d2 INTERFACE
    core_browserengine
    core_wwlib
    core_wwmath
)


# Stage-one integration: compile an independent DX12 diagnostic window,
# receiving real visible gameplay mesh triangles while DX8 remains the
# authoritative (and default) game renderer. Runtime activation additionally
# requires W3D_DX12_PREVIEW=1. No new files are needed for ordinary builds.
option(W3D_ENABLE_DX12_GAMEPLAY_PREVIEW "Build opt-in live W3D geometry mirror using DX12" OFF)
if(W3D_RENDER_BACKEND STREQUAL "DX12" AND W3D_ENABLE_DX12_GAMEPLAY_PREVIEW)
    message(FATAL_ERROR
        "W3D_ENABLE_DX12_GAMEPLAY_PREVIEW requires W3D_RENDER_BACKEND=DX8. "
        "The exclusive DX12 backend cannot mirror an absent DX8 renderer.")
endif()

# Experimental D3D12 device/swap-chain implementation in WW3D2.
# EXCLUDE_FROM_ALL and OFF by default: existing Generals/Zero Hour binaries
# still compile and link exactly the legacy DX8 Renderer implementation.
option(W3D_BUILD_DX12_PRESENTATION "Expose opt-in DX12 presentation library target" OFF)
if(W3D_BUILD_DX12_PRESENTATION OR W3D_ENABLE_DX12_GAMEPLAY_PREVIEW OR
   W3D_RENDER_BACKEND STREQUAL "DX12")
    if(NOT WIN32 OR IS_VS6_BUILD)
        message(FATAL_ERROR "Experimental DX12 presentation requires modern MSVC on Windows")
    endif()
    add_library(w3d_dx12_presentation STATIC EXCLUDE_FROM_ALL
        Backend/DX12/DX12PresentationDevice.h
        Backend/DX12/DX12PresentationDevice.cpp
        Backend/DX12/DX12W3DPassGraph.h
        Backend/DX12/DX12W3DPassGraph.cpp
        Backend/DX12/DX12W3DGeometryBuffers.h
        Backend/DX12/DX12W3DGeometryBuffers.cpp
        Backend/DX12/DX12W3DTextures.h
        Backend/DX12/DX12W3DTextures.cpp
        Backend/DX12/DX12PrimaryWindow.h
        Backend/DX12/DX12PrimaryWindow.cpp
        Backend/DX12/DX12IndexedMeshPass.h
        Backend/DX12/DX12IndexedMeshPass.cpp
    )
    target_compile_features(w3d_dx12_presentation PUBLIC cxx_std_17)
    target_compile_definitions(w3d_dx12_presentation PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(w3d_dx12_presentation PUBLIC d3d12 dxgi dxguid d3dcompiler user32)
endif()


if(W3D_ENABLE_DX12_GAMEPLAY_PREVIEW)
    # Compiled only for explicitly configured Win32 developer builds.
    # The game continues using DX8Backend.cpp for every Renderer method.
    add_library(w3d_dx12_gameplay_preview STATIC
        Backend/DX12/DX12GameplayPreview.h
        Backend/DX12/DX12GameplayPreview.cpp
    )
    target_compile_features(w3d_dx12_gameplay_preview PUBLIC cxx_std_17)
    target_compile_definitions(w3d_dx12_gameplay_preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(w3d_dx12_gameplay_preview PUBLIC
        w3d_dx12_presentation user32
    )
    target_link_libraries(corei_ww3d2 INTERFACE w3d_dx12_gameplay_preview)
    target_compile_definitions(corei_ww3d2 INTERFACE W3D_ENABLE_DX12_GAMEPLAY_PREVIEW=1)
endif()

if(W3D_RENDER_BACKEND STREQUAL "DX12")
    # B03/B04 use this native DX12 device on the game's HWND.
    # Until B07/B10, game initialization fails closed without an explicit B04 diagnostic opt-in.
    target_link_libraries(corei_ww3d2 INTERFACE w3d_dx12_presentation)
    message(STATUS "W3D Renderer: DX12 native frame diagnostic (B04 complete; gameplay not yet supported)")
else()
    message(STATUS "W3D Renderer: DX8 legacy backend")
endif()

