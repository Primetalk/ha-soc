# Fuse selection for the battery monitor

This guide explains the three protection roles shown in the
[hardware schematic](../hardware/battery-monitor-schematic.svg). It does not
approve a particular fuse or replace the battery, BMS, cable, or installation
manufacturer's requirements. A 300 Ah LiFePO4 battery can deliver several
kiloamperes into a short circuit. Have the final protection design reviewed by
a person qualified for the installation and its applicable electrical rules.

## Parameters that every fuse needs

A fuse's printed current is only one of several independent ratings. Record and
verify all of these for the fuse **and its holder**:

1. **DC voltage rating** — must be at least the highest battery/charger voltage,
   including tolerance and credible transients. An AC-only voltage marking is
   not evidence of a DC interrupt rating. For a normal 4S LiFePO4 system,
   `32 VDC` is a common minimum design target, but it is acceptable only after
   confirming that the installation can never exceed it. A higher DC voltage
   rating is safe to use.
2. **Interrupting or breaking rating** (`IR`/`AIC`) — must be at least the
   prospective short-circuit current at the fuse location. This can be many
   thousands of amperes even when the fuse's nominal current is only `0.5 A`.
   Obtain a battery manufacturer's fault-current value or calculate a
   conservative value from the complete source and conductor impedance. Do not
   assume that the BMS will reduce this value unless the BMS manufacturer
   explicitly provides a coordinated protection rating for that fault.
3. **Nominal current and continuous-current derating** — the fuse must carry the
   maximum normal current at the worst enclosure temperature without nuisance
   opening, while remaining low enough to protect the smallest downstream wire,
   connector, PCB trace, and device. Apply the fuse manufacturer's holder,
   temperature, and continuous-load derating rules.
4. **Time-current curve and pre-arcing `I²t`** — a fuse does not open as soon as
   current exceeds its printed value. Its complete clearing curve and energy
   let-through must protect the downstream conductor while tolerating valid
   startup, radio, display, charger, and load surges.
5. **Construction and environment** — use a DC-rated holder with touch/fault
   protection, temperature and vibration ratings, adequate creepage/clearance,
   and a safe enclosure. Do not put an unknown glass fuse or holder directly on
   a high-energy battery tap merely because its ampere rating looks suitable.

## Preliminary targets

These values are **engineering starting points**, not approved substitutions
for the checks above:

| Fuse       | Protection role                                            | Preliminary current target                                                                                                                                                                                                      | Speed                                                                                                                  | Required DC voltage and interrupt ratings                                                                                                                          |
| ---------- | ---------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `F_MAIN`   | Main battery positive conductor, bus, and installation     | No repository default. Size from maximum continuous charge/load current, cable ampacity, surge duration, BMS/contactors, and installation rules. The 500 A shunt rating is **not** the fuse rating.                             | Select from the coordinated main-system time-current study.                                                            | At least the worst-case system voltage and prospective battery fault current; normally a high-interrupt battery fuse system.                                       |
| `F1`, `F2` | The two thin INA219 Kelvin leads                           | `0.5 A` each is the preliminary target; `0.1–1 A` may be reasonable if the selected wire and high-interrupt fuse family require another value. INA219 input current is tiny, so wire protection and breaking capacity dominate. | Fast-acting is normally preferred because there is no intentional inrush, subject to the selected part's actual curve. | At least the worst-case system voltage and the prospective fault current at each energized shunt tap. Both fuses and holders need an explicit DC interrupt rating. |
| `F3`       | Thin nominal `+12 V` input lead and protected DC/DC supply | `1 A` time-delay is the preliminary target for the planned ESP32-C3, INA219, and OLED monitor. Recalculate from the actual converter/rail and reduce it when a smaller rating survives measured startup.                        | Time-delay is often useful for converter input-capacitor inrush; verify with the selected converter and curve.         | At least the worst-case system voltage and prospective fault current at the bus-side tap. It must also protect the actual input wire and converter.                |

The capacity label `300 Ah` describes stored charge, not safe fault current. The
shunt label `500 A / 75 mV` describes its measurement range, not the cable or
fuse rating.

## `F1` and `F2`: Kelvin-sense protection

The normal current into INA219 `VIN+` and `VIN-` is negligible. Therefore,
`F1` and `F2` are selected to protect the thin wire and safely interrupt a
short from either energized shunt tap to common negative or metalwork—not to
supply the INA219.

- Install one fuse in **each** lead, as close as practical to its shunt tap.
- Keep the unfused pigtail between tap and fuse extremely short, separated, and
  mechanically protected.
- Use direct Kelvin taps; do not connect beyond a cable lug where main-current
  voltage drop would enter the measurement.
- Use matched routing and suitable connectors, but do not bridge the two leads.
  The breakout's onboard shunt must remain electrically isolated.
- An opened fuse is a measurement fault. Stop relying on current, power, or SOC
  until both fuse paths and INA219 readings have been checked.

A low ampere rating does **not** make a miniature fuse safe on a battery tap.
For example, a `0.5 A` fuse with a `35 A` interrupt rating is unsuitable where
the prospective short-circuit current is `5 kA`.

## `F3`: monitor-supply input protection

`F3` protects the thin input lead to the DC/DC supply and the supply input. The
schematic takes this feed from the bus side of the shunt so battery-supplied
monitor consumption crosses the shunt and appears as discharge current.

Estimate worst-case converter input current using:

```text
I_input_max = (3.3 V * I_3V3_max) / (V_battery_min * efficiency_min)
              + I_converter_quiescent
```

Then select a fuse that:

1. carries that current after all manufacturer derating;
2. tolerates measured converter input-capacitor inrush;
3. remains below the ampacity/withstand limit of the input wire, connector, PCB,
   and converter; and
4. has the required DC voltage and interrupt ratings.

Illustrative only: a `1.0 A` maximum `+3V3` load, `10 V` minimum battery input,
`80%` minimum converter efficiency, and `10 mA` quiescent current produce about
`0.423 A` input. A `1 A` time-delay fuse can be a reasonable prototype choice
after its derating, inrush, wire protection, and interrupt ratings are verified.
Actual current must be measured during ESP32-C3 Wi-Fi startup and display use.

The DC/DC converter itself must accept the full battery range and transients;
the label `+12 V nominal` does not mean its input is a regulated 12 volts.

## `F_MAIN`: main battery protection

Do not derive `F_MAIN` from firmware settings, battery amp-hours, or the shunt's
500 A measurement range. Its selection belongs to the complete installation:

- maximum simultaneous continuous charging and loading current;
- cable, bus-bar, connector, shunt, disconnect, and contactor ampacity;
- inverter/motor/charger inrush and its duration;
- ambient/enclosure temperature and bundling derating;
- BMS maximum current and fault behavior;
- prospective short-circuit current and required interrupt rating;
- applicable vehicle, marine, stationary-storage, and local electrical rules.

Install the main fuse as close as practical to the battery positive source, with
the battery-to-fuse/shunt assembly enclosed and the unavoidable unfused length
minimized. The schematic is electrical rather than physical; it does not grant
permission for a long unfused battery cable.

## Selection and commissioning record

Complete this table with manufacturer datasheets before energizing the battery:

| Field                                    | `F_MAIN` | `F1` | `F2` | `F3` |
| ---------------------------------------- | -------- | ---- | ---- | ---- |
| Manufacturer and part number             |          |      |      |      |
| Fuse technology / speed                  |          |      |      |      |
| Nominal current                          |          |      |      |      |
| DC voltage rating                        |          |      |      |      |
| DC interrupt rating                      |          |      |      |      |
| Holder part number and ratings           |          |      |      |      |
| Protected wire gauge/type/temp rating    |          |      |      |      |
| Derated wire ampacity                    |          |      |      |      |
| Expected continuous current              |          |      |      |      |
| Maximum valid surge and duration         |          |      |      |      |
| Fuse clearing time at wire fault current |          |      |      |      |
| Prospective short-circuit current at tap |          |      |      |      |
| Installation location / unfused length   |          |      |      |      |
| Design-review reference and approval     |          |      |      |      |

During commissioning, verify no holder or lead heats in normal operation, the
DC/DC supply survives repeated cold starts without nuisance opening `F3`, both
Kelvin paths have continuity, and current/voltage readings remain correct. Do
not intentionally short a production battery to test interrupt performance.
