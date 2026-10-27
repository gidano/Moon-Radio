# Moon Radio vendored SoapESP32

Upstream: https://github.com/yellobyte/SoapESP32

Imported version: 1.4.3, on 2026-10-27. The original MIT license and source
notices are retained. The upstream files are SoapESP32.h/.cpp and
MiniXPath.h/.cpp; SoapMemory.h is the Moon Radio allocation adapter.

Local changes:

- Library-local SoapString and SoapVector use bounded, explicit PSRAM storage,
  without changing Arduino String, global new, or the platform malloc policy.
- Allocation failure is reported instead of falling back to internal RAM or
  aborting through a C++ container allocator. All adapter allocations together
  are limited to 512 KiB. Strings are limited to 32 KiB, vectors to 64 rows;
  the service requests 32 rows and refuses oversized pages. Discovery keeps
  at most eight server candidates.
- Temporary UDP and HTTP request buffers use the same allocation adapter.
- NumberReturned and TotalMatches are exposed for correct short-page offsets.
- Incomplete browse replies return failure; completed lists replace the view
  only on successful completion. Application state is owned by DlnaService.
- Timed reads yield when waiting; connection attempts and XML reads obey the
  operation deadline. Cancellation invalidates results and releases them once
  the in-flight request finishes. The worker is not forcibly deleted.
- HTTP printing uses c_str() to avoid a full-size Arduino String copy.
- IPv4 scan fields are bounded, media objects are value-initialized, root-relative
  resource addresses receive the server address, and HTTP resources may omit
  port 80. Non-HTTP schemes are refused. Media URL hostnames remain unsupported
  by the upstream IPv4 resource parser in this experimental version.
- SOAP object IDs are XML escaped; overly long track IDs are refused.
- The video-class index check is corrected.

The library's optional search/file-download/Wake-on-LAN functions are not used
by Moon Radio. Audio bytes go directly through the existing AudioEngine.
Small socket/FreeRTOS allocations and the currently playing Arduino URL/title
are outside the PSRAM adapter budget. The lazy worker has a separate 8 KiB
PSRAM stack and a small internal control block; after closing the source it
remains asleep so repeated source selection need not recreate a task.
