# API compatibility

Version: 0.9.0-preview. Claims describe current source, not a certification of
any cloud provider.

- OpenAI Chat Completions-compatible JSON `messages`, `model`, `max_tokens`,
  `stream`; Base URL receives `/chat/completions` unless already present.
- SSE `choices[].delta.content`, JSON `choices[].message.content`;
  incremental UTF-8/BOM/CR/LF handling, bounded input and control-character
  rejection. Blank or truncated output is not a successful complete version.
- Exactly one non-streaming fallback only for an explicit unsupported-stream
  400/422 response. It shares the total deadline. No blind retries or redirects.
- HTTPS certificate validation and Windows proxy configuration; HTTP is allowed
  only for loopback endpoints. This tool never starts a local model server.
- Bearer API key, model profiles and separate translation/rewrite roles.
- Thinking setting: automatic omits a vendor switch. Forced enabled/disabled
  is implemented only for `https://api.deepseek.com` on port 443, model
  `deepseek-flash`, and supported root/v1/chat-completions paths. New recognized
  profiles default to disabled; other endpoints/models default to automatic.
  Forced mode on an unsupported endpoint fails locally. This is a source-level
  adapter contract, not proof that the provider currently offers that model.
- Unsupported: Responses API, Anthropic/Gemini native formats, image/vision OCR,
  tools, arbitrary provider-specific reasoning fields and model discovery.

The setting connection test and explicitly requested `--provider-check-online`
send a small request and can incur charges. `--provider-check` and `--diagnose`
are offline checks. HTTP/SSE mock tests use real local WinHTTP with fictional
credentials; they do not verify cloud billing, proxy/TLS behavior or live model
quality. The final binary's cloud-provider acceptance remains UNVERIFIED.

Do not attach API keys, configuration, DPAPI files, OCR text or full HTTP dumps
to an issue. See [security](../SECURITY.md).
