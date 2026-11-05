# Interactive CAPTCHA compatibility work

Goal: display the real Google challenge and let the user complete it inside
Dihscover. This work does not solve challenges automatically or manufacture
verification results. A local test challenge is not proof that Google accepts
the browser.

The image release remains sealed at
`build/dihscover-images-perf-20261103`. Current compatibility candidates are
separate; do not deploy a candidate until its execution tests and packaging
checks pass.

Confirmed missing browser facilities:

1. `EmptyFrameLoaderClient::createFrame` returns no child frame. The native
   client now implements child creation with inherited sandbox flags, referrer
   policy, bounded live frames/depth, views and separate child-load status.
   Its validation covers rendering, click routing, postMessage, cross-origin
   DOM denial, sandboxed opaque origins and X-Frame-Options. Execution validation
   now passes in the combined ARM64 candidate. The first sandbox test exposed a
   fixture error (its script accessed document.body before the body existed);
   the corrected fixture checks actual origin denial and frame pixels.
2. The native resource adapter rejected every non-GET request. An append-only
   SACX operation 40 now accepts GET or memory-only POST, at most 4096 body bytes,
   with the existing network grant. Descriptor, URL, headers and body are copied
   and validated. Host ownership/admission tests pass. WebCore body flattening
   rejects file/blob elements instead of silently omitting them. Queued requests
   retain their body; the broker owns Content-Length framing. ARM64 tests pass
   for the adapter's binary body and queued retry, and for a JavaScript
   XMLHttpRequest POST from a cross-origin frame through the actual SACX broker
   to a modeled server. This is not a live Google or live TLS test.
3. Cookies are disabled, and the old platform cookie stubs terminate if called.
   Do not merely flip the setting. The next implementation needs bounded session
   storage shared by document.cookie and HTTP, authenticated Set-Cookie handling,
   credential-mode enforcement, HttpOnly/Secure/SameSite, host/path scope,
   expiration, prefix rules and public-suffix validation. Clear session cookies
   when the renderer closes. Persistent profiles can follow separately.

After these gates pass, load Google's real public demo without automating a
solution; verify script/frame/image requests and let the user complete the
challenge. Test actual Google Search separately. Record the failing request or
JavaScript API when compatibility is incomplete; do not call it supported merely
because a checkbox paints. Keep certificate verification, WebCore origin/CORS/
sandbox rules and the browser's truthful identity intact.

Google documents its script/widget/form flow in
[reCAPTCHA v2](https://developers.google.com/recaptcha/docs/display), its necessary
`_GRECAPTCHA` cookie in the
[FAQ](https://developers.google.com/recaptcha/docs/faq), and its supported browser
versions in [browser requirements](https://support.google.com/recaptcha/answer/6223828).
Dihscover is outside that listed browser set, so acceptance needs an actual test.

Current isolated commands (WSL Ubuntu):

```sh
python3 tools/webcore_dihos/build_frames.py
python3 tools/webcore_dihos/test_frames.py --include-page
python3 tools/webcore_dihos/build_captcha.py
python3 tools/webcore_dihos/test_captcha.py --include-page
```

The frame builder uses a separate verified header view to make the DihOS empty
client's child-frame hook overridable. It preserves method layout and all engine
archives. Nothing in the working engine cache is edited. The combined candidate
rebuilds networking adapters against operation 40; it requires a matching kernel
before it can be packaged. A successful image release test remains independent
of these experiments.
