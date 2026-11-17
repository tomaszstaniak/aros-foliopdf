# GLSurface sources

MIT-licensed sources pinned by PIN. The local frame API is recorded in
patches/0001-frame-api.patch; apply it to the pinned zunekit source before
comparing src/zk_glsurface.{c,h} with the copies here. It exposes frame
begin/end for Folio's GPU document renderer. No renderer policy or PDF
logic is added to the shared source. This extension has not been submitted
upstream. LICENSE is redistributed with the binary packages.
