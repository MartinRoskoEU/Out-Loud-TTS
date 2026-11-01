# Leading SAPI bookmarks

The adapter collects leading `SPVA_Bookmark` fragments separately from speech.
Names are opaque strings, preserved in order, including duplicates and Unicode.
Empty speech fragments and ignored metadata do not end the leading region.
Bookmarks are never sent to the Rust runtime as text.

When the site requests `SPEI_TTS_BOOKMARK`, the `Speak` caller queues each leading
event through `ISpTTSEngineSite::AddEvents` before writing audio. Events use
`SPET_LPARAM_IS_STRING`, audio offset zero and the existing numeric conversion
of the bookmark name for `wParam`. SAPI supplies the stream number. Plain text
produces no bookmark events.

Cancellation is polled before delivery; failures to query event interest or
deliver events are returned through the existing SAPI error path. This is
leading-bookmark support, not arbitrary word/sentence timing or an application
bookmark parser.

See [the adapter](../sapi/src/adapter.cpp),
[cancellation](SAPI_CANCELLATION.md) and [architecture](ARCHITECTURE.md).
