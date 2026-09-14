# Implementation Plan: A Square-Opening Disc Chopper

Date: 2026-09-05, branch `main`.

CSPEC uses a pair of disc choppers whose opening is square — straight sides, not the
radial edges of a wedge. `CollectorDiskChopper` cannot describe one, and cannot be
extended to, for reasons set out below. This is the plan for a sibling component,
`CollectorSquareDiskChopper`.

## Current state

`CollectorDiskChopper` (readout_core/components/CollectorDiskChopper.comp) takes arbitrary
`slit_edges` and decides transmission by comparing angles on the disc:

```c
double on_disc = collector_chopper_wrap(beam_angle + hit - mark);
/* ... then, per opening, measured from its leading edge: */
if (collector_chopper_wrap(on_disc - opening) < width) { through = 1; break; }
```

That works because a wedge opening — bounded by two radii and two arcs — has the *same
angular width at every radius*, so the test never needs to know how far from the spindle
the ray crossed. It is why one component can carry any number of unevenly spaced openings
for the cost of a loop.

Tests today: `ctest` 109/109, `python3 -m pytest tests/` 47/47, of which twelve cover
`CollectorDiskChopper` across compile, parked, turning, spindle placement and beam angle.

## Why this is a second component, not a `shape` parameter

The obvious move is a string parameter — `shape="wedge"` (default) or `shape="square"`.
It was considered and rejected. Three reasons, in increasing order of weight:

1. **A square opening's width is a chord**, so several square openings of differing widths
   cannot share one `delta_y`. The parameter would silently constrain every opening on the
   disc to the same width.
2. **The geometry differs, not just the test.** For a wedge the beam sits at
   `radius - yheight/2` from the spindle; for a square it sits at
   `radius*cos(angular_width/2) - yheight/2`. The same disc, the same `radius` and the same
   `yheight` put the component in a different place.
3. **The acceptance tests have different shapes.** The wedge's is radius-independent — an
   angle compared against angles. The square's is not: it needs the radius at which the
   ray crossed. That is not a branch inside one test, it is a second test, and a component
   holding both, selected by a string, with two `delta_y` conventions, is two components in
   a trench coat.

The first reason alone is arguable. The third is not.

## The geometry

The disc is viewed from downstream. The opening is symmetric about its own centreline,
which lies at `opening_angle` from the top-dead-centre pickup. Working in coordinates
about the spindle, with `u` across the opening and `v` along it and outward:

```
                    rim of the disc
                 .---------------------.
              .-'    |             |    '-.
            .'       |             |       '.       v = radius*cos(w/2)   corners
           /         |   opening   |         \                            meet the rim
          |          |             |          |
          |          |             |          |
          |          '-------------'          |     v = b                 foot of the
          |             |       |             |                           straight sides
           \            |       |            /
            '.          |       |          .'
              '-.       |       |       .-'
                 '------|-------|------'
                        |       |
                       -a       +a                  u = +/- radius*sin(w/2)
                             spindle
```

with `w` the `angular_width`. Three quantities follow:

- half-chord `a = radius*sin(angular_width/2)` — the straight sides sit at `|u| = a`
- corner height `radius*cos(angular_width/2)` — where a side meets the rim
- foot `b = radius*cos(angular_width/2) - yheight` — the sides run `yheight` down from
  the corners

Above the corners the rim closes the opening, so along the centreline the opening reaches
`radius`; the sketch's top edge is the disc, not a fourth side.

### Where the beam sits

**`delta_y = radius*cos(angular_width/2) - yheight/2`** — the midpoint of the straight
sides, so the beam is centred in the part of the opening with parallel edges.

This is the property that checks it. As the opening narrows, a square becomes a radial
slit and must reduce to the wedge case:

| `angular_width` | `delta_y` |
| --- | --- |
| `0.001` | `0.3200` |
| `5` | `0.3197` |
| `30` | `0.3081` |
| `60` | `0.2731` |
| wedge, `radius - yheight/2` | `0.3200` |

(`radius = 0.35`, `yheight = 0.06`, metres throughout.) A form that does not tend to
`0.3200` is wrong; an earlier draft of this plan carried
`radius*(cos(angular_width/2) - 0.5) - yheight/2`, which tends to `0.1450`.

Centring on the *full* radial extent — including the arc bulging above the corners — is
equally defensible and was the alternative considered. It differs once the opening is
wide: at `angular_width = 60` it puts the beam at `0.2966` rather than `0.2731`. The
straight-side convention was chosen deliberately; revisit it deliberately.

## The acceptance test

With `r` the ray's distance from the spindle and `phi` the signed angle from the opening's
centreline:

```
u = r * sin(phi)          across the opening
v = r * cos(phi)          along it, outward from the spindle

pass  iff  |u| <= a   and   v >= b   and   r <= radius
```

Three inequalities. The middle one is what a wedge cannot express.

`CollectorDiskChopper`'s TRACE already computes everything needed: `rsq` gives `r`, and
`phi` is its `on_disc` taken relative to `opening_angle` and folded to `(-180, 180]`. The
sibling reuses that structure rather than inventing one — the spindle placement, the
`along`/`across` basis, the parked-versus-turning `mark`, and `collector_chopper_wrap` all
carry over unchanged.

### Angles increase toward +x

The same sense as `CollectorDiskChopper`: `hit = atan2(across, along)`, and `opening_angle`,
`park_angle` and `beam_angle` all share the sense that `slit_edges` are measured in.

This matters because `niess`'s `disc_beam_offset` documents its turn as counter-clockwise
about `+z`, which is the opposite, so a caller may need to negate on the way in.
`tests/test_run_components.py::TestRunCollectorDiskChopper::test_a_beam_angle_is_a_shift_of_the_openings`
pins the sense in both directions and is the reference for getting it right.

### Validity, to check in INITIALIZE

- `angular_width > 0`
- bottom corners inside the rim: `yheight <= 2*radius*cos(angular_width/2)`
- opening does not span the spindle: `yheight <= radius*cos(angular_width/2)`

The second is the weaker of the two and is implied by the third; check the third and say
which it is.

## What a square opening does that a wedge cannot

At radius `r` the opening's angular half-width is `asin(a/r)`, which grows toward the
spindle. At `angular_width = 30`, `radius = 0.35`, `yheight = 0.06`:

| radius crossed | angular half-width |
| --- | --- |
| `0.3500` (rim) | `15.00 deg` |
| `0.3081` (beam) | `17.10 deg` |
| `0.2781` (foot) | `19.01 deg` |

A neutron crossing near the hub sees the chopper open longer than one crossing at the rim.
That is the physical signature of a square opening, the reason it cannot be written as
`slit_edges`, and — see below — the only thing that distinguishes the two shapes in a test.

## Phases

### Phase S1 — The component (the core deliverable)

New file `readout_core/components/CollectorSquareDiskChopper.comp`. `DEFINE COMPONENT`
must match the filename stem: the docs generator takes the H1 and anchor from the former
and the page name and index link from the latter, and a mismatch produces a page that
disagrees with the link pointing at it.

Parameters, following the family:

```
angular_width   [deg]  full angular width of the opening, subtended at the rim
opening_angle   [deg]  centre of the opening, from the zero mark
radius          [m]    outer radius of the disc
yheight         [m]    height of the straight sides, down from the rim
nu              [Hz]   signed rotation frequency; 0 parks the disc
delay           [s]    when the zero mark is on the beam
park_angle      [deg]  where the zero mark stands while parked
beam_angle      [deg]  where the beam crosses the disc, from the zero mark
zero_angle      [deg]  from the component +y to the top-dead-centre pickup
jitter          [s]    per-ray timing jitter
abs_out         [1]    absorb rays that miss the disc entirely
tdc_pv          [str]  control-system channel for this disc's TDC times
filename        [str]  HDF5 output file
dataset_name    [str]  collector group name
verbose         [1]    -1 silent to 3 details
```

The tail four are common to every collector and appear in that order with identical `%P`
wording. There is no `slit_edges` and no `n_edges`: one opening, described by its width and
where it sits.

Records nothing per ray, exactly as `CollectorDiskChopper` does — a collector group with
`ess_type 0` so replay announces it and steps over it, and one sink string
`<COMP_NAME>_chopper_tdc` carrying `tdc_pv`. Top-dead-centre times are derived at replay
from `nu` and `delay`, which the file already holds as instrument parameters.

`MCDISPLAY` should draw the rim about the spindle and the opening as four segments, so the
straight sides are visible as straight.

Acceptance: `ctest` still 109/109; the component compiles through mccode-antlr in both
turning and parked configurations.

### Phase S2 — Tests (where the real risk is)

A class each in `tests/test_compile_components.py` and `tests/test_run_components.py`,
following `TestCompileCollectorDiskChopper` and `TestRunCollectorDiskChopper`. Reuse the
`_transmitted` helper: assert *counts through a monitor*, never that the run completed.
Three of the four wedge tests carry over unchanged in shape:

- **parked open and parked closed** — the opening on the beam, then the solid disc.
- **a turning disc chops in time** — two delays half a rotation apart, asserting
  `sorted(counts) == [0, RAYS]` without saying which is which, because that depends on the
  neutron velocity.
- **the spindle lies where the angles put it** — beam displaced in `+y` with `abs_out=0`,
  so a ray beyond the rim passes and one inside the hub does not, and the two sides of the
  disc behave oppositely. Note the source has width, so this asserts that one side
  transmits and the other does not, not a precise fraction.

Two are specific to this component and are the ones worth writing first:

- **the shape is square, not a wedge.** Two pencil beams crossing at different radii, at a
  phase where the opening is closing. A wedge passes or blocks both together; a square
  passes the inner one after it has blocked the outer. Nothing else in the test suite can
  tell the two shapes apart, so without this the component is only being tested as an
  expensive wedge.
- **the wedge limit.** A very narrow square opening must transmit like the equivalent
  narrow wedge in `CollectorDiskChopper`, run as a second instrument. This ties the two
  components together and is what catches a `delta_y` slip — which, given that this
  document began with one, is not hypothetical.

Acceptance: `python3 -m pytest tests/` green, with the two shape-specific tests
demonstrated to fail against a deliberately wedge-shaped acceptance test.

### Phase S3 — Prose (small)

The component lists in `README.md`, `AGENT.md`, `CLAUDE.md`, `docs/architecture.md` and
`docs/integration.md` are hand-maintained and **already stale**: none of them mentions
`CollectorDiskChopper`. Add both at once.

Nothing else needs registering. CMake globs `components/*.comp` and `extract_comp_docs.py`
globs the same directory, so install, build-tree copy and the generated docs page all
follow automatically — though `file(GLOB)` runs at configure time, so a reconfigure is
needed before the install list picks the new file up. `readout_type_descriptions.h` is not
involved: like `CollectorDiskChopper` this is not EFU-sendable and uses `ess_type 0`, which
is what keeps it out of the anti-drift registry test.

Acceptance: `cmake -S . -B <scratch> -DREADOUT_DOCS_ONLY=ON && cmake --build <scratch>
--target docs` produces a page for the component with every parameter documented — the
generator warns on stderr for any `SETTING PARAMETERS` entry missing from `%P`.

## Known risks / open questions

- **One opening is a scoped assumption, not a fundamental one.** Nothing in the geometry
  forbids several identical square openings; they would share `a`, `b` and `delta_y` and
  cost only a loop over `opening_angle`s. If CSPEC or anything else needs two, that is a
  parameter, not a rewrite. It is left out because nothing needs it yet.
- **`delta_y` is a convention.** Recorded above with its alternative and the numbers that
  separate them. A future reader who disagrees should change it knowingly.
- **The NeXus representation is unsettled and deliberately out of scope.** `NXdisk_chopper`
  describes openings only as `slit_edges`, which are angles; a square opening has no single
  correct pair, because its angular width depends on the radius the neutron crossed at. Any
  emitted `slit_edges` is therefore lossy and needs a stated radius to be quoted at. Settle
  it when `niess` grows a square-disc class, not before.
- **Sub-tick timing is not a concern here but is worth knowing.** Replay quantises to the
  88.052499 MHz EFU clock, about 11 ns, which is far below any chopper timescale.
