# Charging phase and automatic full-charge synchronization

The monitor infers a charging process from fresh **battery-terminal voltage and
net battery current**. It exposes `Charging Phase` and `Full Charge Detected` in
Home Assistant. `Automatic Full Charge Sync` starts on after every reboot: a
qualified full detection immediately sets remaining capacity to rated capacity,
marks SOC valid, and stages the normal SOC checkpoint. This can correct an
estimate such as 90% when the measured charging process indicates full.

The phases are inferred; they are not telemetry from the charger or BMS. The
monitor cannot establish individual cell balance or distinguish every charger,
BMS, load, and measurement fault using pack voltage/current alone. No hardware
charging trace has yet validated this detector.

## Observed process

The defaults use a rolling five-minute window, retaining 61 voltage/current
points at roughly five-second intervals, retaining all voltage extrema between
points. Every incoming sample, not just those points, checks polarity, freshness, and uninterrupted tail-current dwell.

1. At least 60 seconds at **5 A or more** establishes substantial charging. This
   accommodates the observed 30–40 A charging without requiring a specific
   charger rating. With five minutes of approximately constant current (maximum
   no more than 120% of minimum), the inferred phase becomes `cc`.
2. A `cv` phase needs a five-minute high-voltage plateau with no more than
   **0.15 V total variation**, and an average current in its last minute at least
   **20% below** its first minute. At least 60 continuous seconds must also have
   been observed at intermediate current: above the tail threshold and at most
   80% of the observed charging peak. This rejects an abrupt jump directly from
   high current to a tiny current. Seeing a `cc` label first is not mandatory;
   observing a sufficiently substantial part of CV can establish the process.
3. After CV evidence, high voltage and **strictly positive current at or below
   the tail threshold** start `almost_full`. Five uninterrupted minutes qualify
   `full`, turn on `Full Charge Detected`, and request SOC synchronization.
4. While that condition continues, synchronization repeats every five minutes.
   The full indicator stays on between repeats. Integration remains unbounded,
   so a tiny increase above 100% between synchronizations is expected.

The voltage must stay above the configured minimum and within 0.15 V of the
observed CV plateau midpoint after qualification. Zero/negative current, invalid
samples, a data gap longer than five seconds, loss of the plateau, or reboot
clear the process history. A rise above the tail threshold clears the full
indicator and restarts the tail dwell; CV evidence can remain if the high-voltage
plateau is intact. A return to at least 80% of the observed charging peak clears
CV evidence immediately. Pending taper evidence also clears on renewed bulk
current or voltage below the minimum, and any newly observed constant-current plateau above the tail threshold
replaces old taper evidence, including currents below the 5 A active threshold. Intermediate current that has not shown a fresh
qualifying taper within another five-minute window also clears that evidence.
There is no inference across missing data or restart downtime.
A device starting up with only high voltage and 0.2 A will remain `observing`
until it witnesses a qualifying charging process.

`Full Charge Detected` describes the **current qualified charging condition**,
not a permanent battery-full latch. It turns off when current stops or the
conditions cease, and becomes unavailable when measurements are missing/invalid.
The last synchronized SOC remains valid and resumes normal coulomb counting.
The other phase values are `observing`, `idle`, `discharging`, and `unavailable`.
The existing `Battery Charging` indicator has its own 0.2 A deadband; that deadband
is deliberately not applied to this detector's positive tail current.

## Settings and initial values

| Home Assistant control | Initial value | Meaning |
| --- | --- | --- |
| Full Charge Voltage | 14.4 V | Minimum battery voltage during the CV plateau and tail dwell |
| Full Charge Tail Current | 0.3 A | Maximum strictly positive current during the tail dwell |
| Automatic Full Charge Sync | On after reboot | Allows a qualified detection to synchronize SOC |

Voltage and tail settings persist across reboot. These defaults follow the observed **14.616 V / 0.2 A** endpoint and requested
14.4 V / 0.3 A thresholds for this 4S LiFePO4 installation. They are not
manufacturer-verified endpoints for an identified battery/charger model. Use a
complete charging trace and the battery's endpoint requirements to tune them.
For comparison, [Victron's SmartShunt settings](https://www.victronenergy.com/media/pg/SmartShunt/en/all-features-and-settings.html)
recommend 13.9 V charged voltage for nominal 12 V lithium systems and default to
4% capacity tail current (12 A for 300 Ah). Those are monitor synchronization
criteria, not universal LiFePO4 charging endpoints. Our 0.3 A criterion is much
stricter (0.1% of 300 Ah) and follows the observed installation. The two runtime
controls currently allow 12–16 V and 0.05–4 A; tail current must remain below the
substantial-charging threshold.

The active-current threshold, voltage stability band, and five-minute observation/
dwell/repeat duration are substitutions in
[`../packages/battery-config.yaml`](../packages/battery-config.yaml).

With the configured 75 mV / 500 A shunt, 0.2 A produces only 30 microvolts. The
INA219's shunt-voltage step is 10 microvolts; its specified input offset can
exceed that 30-microvolt signal. Averaging does not remove systematic offset.
Check the installed monitor's zero-current offset and tail readings against a
reference before treating this as a reliable endpoint detector. See the
[TI INA219 datasheet](https://www.ti.com/lit/ds/symlink/ina219.pdf) and the
[measurement commissioning procedure](commissioning.md).

Changing either threshold, toggling automatic synchronization, or using any
manual SOC button clears the accumulated detection history. Turning automatic
synchronization off leaves phase/indicator observation running but suppresses
SOC updates. Turning it back on requires new evidence. The switch is intentionally
not restored: it is on again after reboot. To keep SOC manually invalid during
diagnosis, turn automatic synchronization off for that session.

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
recent correction. New persistent threshold settings are allocated after all
existing ESP8266 preferences, preserving their offsets for upgrades/rollback.

## Verification on the installed system

Record battery voltage, current, Charging Phase, Full Charge Detected, and SOC
through one complete charge. Check that the phase sequence follows the measured
taper, full detection occurs only after the positive tail dwell, and SOC changes
to 100%. If charging continues, check a second adjustment five minutes later.
Also check that charger shutdown, missing measurements, and a restart during
charging cancel detection rather than manufacturing a full endpoint. Compare
against independent battery/charger evidence; the automated host tests and
firmware builds do not replace this physical verification.
