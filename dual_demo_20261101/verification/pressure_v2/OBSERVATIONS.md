# Board telemetry observations (not automatic acceptance)

Input SHA-256: `b65088a73a2df5f35e644fa1efa4f1b816b80ce05f495bd5239deb64e70b6cc9`
TEL rows: 418; paired JOY snapshots: 418; parse errors: 0.
Longest active-pressure boundary span: 324 nominal seconds.
Five-minute candidate intervals: 1.
Valid/armed active direction snapshots: {'neutral': 295, 'left': 24, 'right': 36, 'up_left': 9, 'down_right': 12, 'up_right': 3, 'down_left': 39}.
Observed effective fire-request snapshots: 97.
Stable-interval cumulative counter growth: {'sc': 37100, 'abs': 9045, 'hit': 126}.

Full intervals, faults, recovery counters and frame statistics: `analysis.json`.

- JOY is one snapshot per nominal second; it cannot measure each ADC interval, subsecond release or uninterrupted physical hold.
- jg=99999 is an unavailable/saturated sentinel, including when the caller now precedes a later last_fresh update; it alone is not proof that sampling stopped.
- TEL fr counts completed/displayed frames in the preceding firmware window; it is approximately FPS only for uninterrupted one-second windows. Transition windows are separated.
- A pressure interval is sampled continuity evidence. First boundary row is excluded from its nominal duration; timestamps are host receive times, not FPGA cycle timestamps.
- hp edges are observed starts, not a complete restart acceptance test. Use per-round flow/display/input evidence separately.
- mask bit 32 proves an observed effective fire request; TEL has no separate fired/displayed projectile count.
- abs growth proves shield absorption. No abs growth does not prove shield toggle failure; TEL/JOY omit toggle state.
- score growth implies player-bullet kills and explosion spawn requests from audited combat.c. It does not prove every explosion was rendered; effect pool may merge/drop effects.
- Input directions are from effective input_mask and could also originate from UART input if another sender supplied it; physical-only attribution requires session/operator evidence.
