# Charging phase and automatic full-charge synchronization

The monitor infers a charging process from fresh **battery-terminal voltage and
net battery current**. It exposes `Charging Phase` and `Full Charge Detected` in
Home Assistant. `Automatic Full Charge Sync` defaults to on at first use and remembers your
choice across reboots. When enabled, a
qualified full detection immediately sets remaining capacity to rated capacity,
marks SOC valid, and stages the normal SOC checkpoint. This can correct an
estimate such as 90% when the measured charging process indicates full.

The phases are inferred; they are not telemetry from the charger or BMS. The
monitor cannot establish individual cell balance or distinguish every charger,
BMS, load, and measurement fault using pack voltage/current alone. No hardware
charging trace has yet validated this detector.

## Observed process

The defaults use a rolling five-minute voltage/current history, retaining 61
points at roughly five-second intervals and all voltage extrema between points.
Every incoming sample checks freshness and current polarity. SOC and energy
integration continue using the original current; averaging is only for deciding
whether the full-charge tail condition is met.

1. At least 60 seconds at **5 A or more** establishes substantial charging. This
   accommodates the observed 30–40 A charging without requiring a specific
   charger rating. With five minutes of approximately constant current (maximum
   no more than 120% of minimum), the inferred phase becomes `cc`.
2. Recognizing `cv` initially needs a five-minute high-voltage window with no more
   than **0.15 V total variation**, and an average current in its last minute at
   least **20% below** its first minute. At least 60 continuous seconds must also
   have been observed at intermediate current: above the tail threshold and at
   most 80% of the observed charging peak. An abrupt CC-to-tiny-current drop
   alone does not qualify. Seeing a `cc` label first is not mandatory.
3. Once recognized, CV evidence survives a slow or flat positive-current tail.
   Another 20% decline every five minutes is **not** required. Voltage stability
   is evaluated over the **latest five minutes**, not against a fixed initial
   CV voltage. Thus a gradual rise from about 14.4 V toward 14.616 V can qualify.
4. A **30-second time-weighted current average** must be positive and at or below
   the tail threshold, with voltage above the configured minimum and no more
   than 0.15 V variation in the latest five minutes. These conditions start
   `almost_full`. Five uninterrupted minutes qualify `full`, turn on
   `Full Charge Detected`, and request SOC synchronization. The average uses
   thirty completed one-second buckets, so it can lag the newest sample by up
   to one second; it must warm up before tail qualification begins.
5. While those conditions continue, synchronization repeats every five minutes.
   The full indicator stays on between repeats. Integration remains unbounded,
   so a tiny increase above 100% between synchronizations is expected.

A rising or falling voltage window wider than 0.15 V pauses full qualification
and resets its dwell, but retains established CV evidence. Zero/negative current
is checked on **every raw sample**, even if its average would still be positive.
It clears the process history immediately. Invalid samples, a data gap longer
than five seconds, reboot, voltage below the charged threshold after recognizing
CV, an abrupt voltage step greater than 0.15 V between consecutive samples, or
a return to at least 80% of the charging peak also discard the history.

Current averaging allows small individual steps above the tail threshold while
the average remains below it. A sustained rise that takes the average above the
threshold clears `Full Charge Detected` and restarts the dwell; established CV
can remain. Before CV has been recognized, a newly observed constant-current
plateau above the tail threshold replaces pending taper evidence, including
currents below 5 A. This prevents an old, incomplete taper from qualifying an
abrupt charger cutoff.

There is no inference across missing data or restart downtime. A device starting
up with only high voltage and low current remains `observing` until it witnesses
a qualifying charging process. Network disconnection alone does not reset the
local detector; missing sensor samples or an actual reboot do.

`Full Charge Detected` describes the **current qualified charging condition**,
not a permanent battery-full latch. It turns off when current stops or the
conditions cease, and becomes unavailable when measurements are missing/invalid.
The last synchronized SOC remains valid and resumes normal coulomb counting.
The other phase values are `observing`, `idle`, `discharging`, and `unavailable`.

## Current-direction indicators

`Battery Charging`, `Battery Discharging`, and `Battery Idle` share one mutually
exclusive direction state. Charging starts after **ten continuous seconds above
0.3 A**, and ends after **ten continuous seconds below 0.1 A**. Discharging uses
the symmetric negative thresholds. A sustained reversal can change directly
between charging and discharging. Threshold equality holds the previous state;
values between the thresholds do not toggle an already established direction.
Missing or invalid current clears the state and pending transition.

These are significant-current indicators, separate from the charging-process
phase. A small positive tail can therefore show `Battery Idle` while the phase
is `almost_full` or `full`. Alternating 0.259/0.198 A will retain an established
charging indication; 0.137/0.061 A will not turn it on from idle. Once charging,
those latter readings alternating every five seconds will also retain charging
until the current stays below 0.1 A for a full ten seconds. No small current is
removed from SOC/energy integration.

## Settings and initial values

| Home Assistant control | Initial value | Meaning |
| --- | --- | --- |
| Full Charge Voltage | 14.4 V | Minimum battery voltage during the CV plateau and tail dwell |
| Full Charge Tail Current | 0.5 A | Maximum positive 30-second average during the tail dwell |
| Automatic Full Charge Sync | On at first use; choice restored | Allows a qualified detection to synchronize SOC |

Voltage and tail settings persist across reboot. **An existing device keeps its
saved tail threshold after upgrading.** Set `Full Charge Tail Current` to **0.5 A**
in Home Assistant to adopt the new recommendation; the firmware does not
silently overwrite a chosen threshold. The voltage default remains **14.4 V**.

These proposed defaults follow the observed **14.616 V** endpoint and discrete
current steps such as 0.336/0.259/0.198 A for this 4S installation. They are not
manufacturer-verified endpoints for an identified battery/charger model. The
0.5 A threshold leaves margin above those steps and is about 0.17% of 300 Ah.
Use a complete charging trace and the battery's endpoint requirements to tune
these settings.
For comparison, [Victron's SmartShunt settings](https://www.victronenergy.com/media/pg/SmartShunt/en/all-features-and-settings.html)
recommend 13.9 V charged voltage for nominal 12 V lithium systems and default to
4% capacity tail current (12 A for 300 Ah). Those are monitor synchronization
criteria, not universal LiFePO4 charging endpoints. Our 0.5 A criterion is much
stricter and follows the observed installation. The two runtime
controls currently allow 12–16 V and 0.05–4 A; tail current must remain below the
substantial-charging threshold.

The active-current threshold, voltage stability band, five-minute observation/
dwell/repeat duration, and current-direction hysteresis/delay are substitutions in
[`../packages/battery-config.yaml`](../packages/battery-config.yaml).

With the configured 75 mV / 500 A shunt, 0.2 A produces only 30 microvolts. The
INA219's shunt-voltage step is 10 microvolts; its specified input offset can
exceed that 30-microvolt signal. Averaging does not remove systematic offset.
The 10-microvolt step corresponds to about 0.067 A with this shunt.
Averaging tolerates quantization but does not correct a zero-current offset.
Check the installed monitor's zero-current offset and tail readings against a
reference before treating this as a reliable endpoint detector. See the
[TI INA219 datasheet](https://www.ti.com/lit/ds/symlink/ina219.pdf) and the
[measurement commissioning procedure](commissioning.md).

Changing either threshold, toggling automatic synchronization, or using any
manual SOC button clears the accumulated detection history. Turning automatic
synchronization off leaves phase/indicator observation running but suppresses
SOC updates. Turning it back on requires new evidence. The switch remembers its on/off setting across reboots. To keep SOC manually
invalid during diagnosis, turn automatic synchronization off. Changes use the
normal flash-write coalescing interval; an immediate power cut before the write
finishes can lose the latest setting.

## SOC rules and persistence

Detection does not depend on SOC or `SOC Valid`. Manual and automatic full
anchors share the same update/checkpoint logic and rebuild rule conditions
without synthesizing a crossing event. Logs distinguish automatic observations
from manual operator assertions. The full indicator is published before the SOC
update, so a controller reacting to a resulting stop-charge request can also
see the qualified full condition.

The default Stop Charge level is still 95%. If actual charging continues while
the estimate is below that level, detection can correct it to 100%, as in the
reported 90% case. If a controller stops charging at 95% before a qualified
endpoint, this detector cannot observe completion and does not override that
controller. Periodic full charging would require a separate explicit policy.

Process history is RAM-only. The existing SOC checkpoint cadence and flash
coalescing apply to automatic anchors too; a power cut before flush can lose a
recent correction. Persistent threshold settings follow the legacy ESP8266 preferences; the
automatic-sync switch preference follows both thresholds. Existing offsets are
preserved for upgrades/rollback.

## Verification on the installed system

Record battery voltage, current, Charging Phase, Full Charge Detected, and SOC
through one complete charge. Check that the phase sequence follows the measured
taper, full detection occurs only after the positive tail dwell, and SOC changes
to 100%. If charging continues, check a second adjustment five minutes later.
Also check that charger shutdown, missing measurements, and a restart during
charging cancel detection rather than manufacturing a full endpoint. Compare
against independent battery/charger evidence; the automated host tests and
firmware builds do not replace this physical verification.
