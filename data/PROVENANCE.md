# Example data: provenance, units and schema notes

All files in this folder were **created by the candidate's own tool** (`wb_cli gen-examples`, source
in `src/cli/main.cpp`). No third-party or proprietary data is included. They are released as CC0-1.0.
Regenerate them with `wb_cli gen-examples data`.

## `heatsink_fins.stl` (geometry, valid)

* ASCII STL, solid name `finned_heatsink_mm`, 156 triangles.
* **Units: millimetres** (STL itself is unitless; choose "mm" on import).
* Geometry: a comb-shaped profile extruded 40 mm along z. Base 60 x 6 mm, six fins 2 mm thick and
  30 mm tall at 10 mm pitch.
* Constructed as a closed 2-manifold. Every cap vertex is also a wall vertex, so there are no
  T-junctions. Expected values, asserted in the tests: closed, 0 boundary edges, volume = 720 mm^2 x
  40 mm = 28,800 mm^3, bounding box 60 x 36 x 40 mm.

## `heatsink_nodal_temperature_SYNTHETIC.csv` (nodal results, valid, **SYNTHETIC**)

* **Not an ANSYS export.** It is a self-created synthetic field in the ANSYS nodal-export schema
  required by the brief: `node_id,x_m,y_m,z_m,temperature_C`.
* Units: coordinates in **metres**, temperature in **degrees Celsius**. 8,274 nodes, ids 1..8274.
* Nodes lie on a 2 mm grid inside the heat-sink volume above (converted to metres).
* Temperature model, chosen for plausibility rather than accuracy:
  * base: 85 C at the heated bottom face, falling by 2.5 C across the 6 mm base, with a mild
    parabolic spread towards the edges;
  * fins: the adiabatic-tip fin solution `T = Ta + (Tb - Ta) cosh(m(Lf - s)) / cosh(m Lf)` with
    Ta = 25 C, m = 45 1/m, Lf = 30 mm;
  * a small downstream warming term along z.
* Range: 51.7 C to 85.0 C. There is no mesh-to-result registration with the STL, which the brief does
  not require. The coordinates simply share the same frame.

## How a real ANSYS export maps to this schema

In ANSYS Mechanical, select the Temperature result, then right-click → *Export → Export Text File*.
The exported columns are *Node Number, X Location, Y Location, Z Location, Temperature*. Rename the
header to `node_id,x_m,y_m,z_m,temperature_C`, make sure the unit system is metres and °C, and save
as comma-separated with a `.` decimal separator. The importer matches columns by name, so the order
does not matter. Semicolon-delimited files (a common regional Excel setting) are detected and
explained.

## `plate_open.stl` (valid, open mesh)

Two triangles forming a 10 x 10 plate. Used to demonstrate the "open mesh" warning, which is non-fatal.

## `malformed/` (negative test corpus, each must be rejected)

| File | Defect | Expected diagnostic |
|---|---|---|
| `stl_empty.stl` | 0 bytes | `stl.empty` |
| `stl_nan_vertex.stl` | `nan` coordinate (line 5) | `stl.non_finite`, line 5 |
| `stl_truncated.stl` | ends inside a facet | `stl.truncated` |
| `stl_four_vertices.stl` | quad facet | `stl.too_many_vertices` |
| `stl_decimal_comma.stl` | `1,5` | `stl.bad_number` |
| `stl_no_facets.stl` | empty solid | `stl.no_facets` |
| `stl_binary.stl` | binary STL | `stl.binary` |
| `csv_empty.csv` | 0 bytes | `csv.empty` |
| `csv_header_only.csv` | no rows | `csv.no_rows` |
| `csv_bad_header.csv` | wrong column names | `csv.missing_column` |
| `csv_nan_temperature.csv` | `NaN` | `csv.non_finite` |
| `csv_duplicate_node.csv` | repeated id | `csv.duplicate_node` |
| `csv_semicolon.csv` | `;` delimiter | `csv.semicolon` |
| `csv_wrong_column_count.csv` | missing value | `csv.column_count` |
| `csv_below_absolute_zero.csv` | -300 C | `csv.below_absolute_zero` |

`manifest.json` lists every file for the UI's example picker.
