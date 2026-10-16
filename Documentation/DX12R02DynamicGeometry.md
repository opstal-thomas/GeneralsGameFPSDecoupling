# DX12-R02 — pooled transient geometry checkpoint

The exclusive native DX12 backend now suballocates dynamic vertex and index
snapshot generations from reusable, 16-byte-aligned 1 MiB D3D12 UPLOAD pages.
Oversized writes receive their own page. Each committed W3D buffer update
takes a non-overlapping range so DISCARD and NO_OVERWRITE do not modify data
addressed by earlier draw commands. The underlying upload page is retained
by the existing presentation frame resource fence. The IA vertex/index views
add the suballocation offset to the W3D first-vertex / first-index offsets.

Pages used by a previous recording are recycled only once the last submitted
queue fence has completed. Pages belong to a specific ID3D12Device instance,
preventing sharing across device recreation. Static geometry remains in
DEFAULT heaps, using the original R01 CopyBufferRegion staging path.

## Verification status

**CI repair (2026-10-16):** The initial R02 source-contract check incorrectly
prohibited `newNative->Map`, despite that being the correct way to obtain a
CPU pointer to a pooled UPLOAD page. The regression assertion was corrected.
The Win32/x64 WARP geometry smoke now copies the first vertex snapshot, the
NO_OVERWRITE vertex snapshot, and the shared-page index range into independent
GPU readback buffers, checks all three byte sequences after the frame fence,
and checks page reuse after an idle fence and resize. This is committed test
coverage, not a claim of a completed CI run.

The R01 Python source contract now checks pooled page allocation, device
identity, fence gating and view offsets. The WARP geometry smoke's R01
whole-resource identity assertion was updated for R02 page sharing; it now
reads back both generations so the immutability guarantee is demonstrated
through *nonoverlapping GPU addresses* instead of resource identity.

**Windows compilation and WARP execution have not been run in this session.**
The R02 package remains provisional pending the existing Windows game build,
source audit and x86/x64 WARP workflow, plus a GPU address/data readback test
covering two dynamic generations and reuse after a completed fence.

This resource change does not integrate sorting, terrain tracks or particles
with authoritative DX12 draw submission (M01/B07).
