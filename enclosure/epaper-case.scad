// ============================================================================
//  ESP32 e-paper weather display — parametric enclosure
//  Three Oak Woods · esp32-epaper-web
//
//  Two printed parts: a SHELL (back + walls; holds the electronics + 18650)
//  and a FRONT plate (display window). They screw together at 4 corners.
//
//  HOW TO USE
//    1. MEASURE your actual parts and update the dims below (defaults are
//       typical but every board/holder varies a little).
//    2. Set `part` to "shell", "front", or "all" (all = laid out for preview).
//    3. F5 preview, F6 render, then File > Export > STL.
//    Or from the command line (see enclosure/README.md):
//       openscad -D 'part="shell"' -o shell.stl epaper-case.scad
//
//  Units: millimetres.
// ============================================================================

part = "all";          // "all" | "shell" | "front"

/* [Global] */
wall   = 2.0;          // wall / floor thickness
tol    = 0.4;          // clearance for press-fits
fillet = 3.0;          // outer corner radius
eps    = 0.1;          // tiny overlap to avoid coincident faces
$fn    = 64;

/* [E-paper display module] */
disp_w  = 59.0;        // module outline width  (X)
disp_h  = 29.5;        // module outline height (Y)
disp_t  = 1.6;         // module thickness seated in the front pocket
win_w   = 49.0;        // visible active-area window width
win_h   = 24.0;        // visible active-area window height
win_dx  = 0.0;         // window offset from module centre (X)
win_dy  = 0.0;         // window offset from module centre (Y)
win_lip = 1.8;         // faceplate thickness around the window (retains module)

/* [ESP32 DevKit V1 (30-pin)] */
esp_l        = 52.0;   // PCB length (X)
esp_w        = 28.5;   // PCB width  (Y)
esp_h        = 13.0;   // height incl. headers (Z clearance)
esp_standoff = 4.0;    // standoff post height under the PCB
esp_post_d   = 4.5;    // standoff post diameter
esp_pin_d    = 2.0;    // self-tap pilot in the post

/* [18650 holder] */
batt_l  = 76.0;        // holder length incl. terminals (X)
batt_w  = 20.0;        // holder width (Y)
cell_d  = 18.7;        // bare cell diameter (for the cradle saddles)

/* [Charger module: IP5306 board or Adafruit PowerBoost 1000C] */
chg_l   = 40.0;        // board length
chg_w   = 25.0;        // board width

/* [Ports] */
usb_w   = 12.0;        // charge-port cutout width  (bottom wall)
usb_h   = 7.0;         // charge-port cutout height
sw_d    = 7.0;         // power-switch hole dia on a side wall (0 = omit)

/* [Corner screws] */
scr_d      = 2.6;      // self-tap shaft into the posts (M2.5/M3 self-tapping)
scr_clear  = 3.2;      // clearance hole through the front plate
scr_head   = 6.0;      // countersink dia
post_d     = 7.0;      // corner post diameter

// ---------------------------------------------------------------------------
//  Derived cavity / outer dimensions
// ---------------------------------------------------------------------------
margin  = 3.0;
inner_x = max(batt_l, esp_l, disp_w, chg_l) + 2*margin;
inner_y = batt_w + esp_w + 3*margin;
inner_z = max(cell_d, esp_h) + 2;                   // internal depth (front-to-back)

out_x   = inner_x + 2*wall;
out_y   = inner_y + 2*wall;
shell_z = inner_z + wall;                            // back wall + cavity
front_t = win_lip + disp_t;                          // front plate total thickness

// component Y bands (relative to centre): battery low, ESP high
batt_cy = -inner_y/2 + batt_w/2 + margin;
esp_cy  =  inner_y/2 - esp_w/2  - margin;

// corner post centres — pulled in 2 mm so the post overlaps (not just touches)
// the side walls, giving a clean manifold union
px = inner_x/2 - 2;
py = inner_y/2 - 2;

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
module rbox(x, y, z, r) {            // rounded box, sits on z=0, centred in XY
  linear_extrude(z)
    offset(r) offset(-r)
      square([x, y], center=true);
}

// ---------------------------------------------------------------------------
//  SHELL  (back + walls + internal mounts)
// ---------------------------------------------------------------------------
module shell() {
  difference() {
    rbox(out_x, out_y, shell_z, fillet);
    // hollow the cavity, open at the front (+Z); over-cut past the top
    translate([0,0,wall])
      rbox(inner_x, inner_y, shell_z + 1, max(0.1, fillet-wall));
    // charge port on the bottom wall (-Y)
    translate([0, -out_y/2, wall + usb_h/2 + 1])
      rotate([90,0,0]) cube([usb_w, usb_h, wall*4], center=true);
    // optional power-switch hole on the right wall (+X)
    if (sw_d > 0)
      translate([out_x/2, esp_cy, wall + esp_h/2])
        rotate([0,90,0]) cylinder(d=sw_d, h=wall*4, center=true);
  }

  // corner posts (full depth, pilot-drilled for the front screws)
  for (sx=[-1,1], sy=[-1,1])
    translate([sx*px, sy*py, 0])
      difference() {
        cylinder(d=post_d, h=shell_z);
        translate([0,0,wall]) cylinder(d=scr_d, h=shell_z);
      }

  // 18650 cradle: two saddle walls with a semicircular cut for the cell
  for (sx=[-1,1])
    translate([sx*batt_l*0.30, batt_cy, wall])
      difference() {
        translate([-4, -batt_w/2, -eps]) cube([8, batt_w, cell_d*0.7 + eps]);
        translate([0, 0, cell_d*0.7 + cell_d/2 - 1])
          rotate([90,0,90]) cylinder(d=cell_d+tol, h=20, center=true);
      }

  // ESP32 standoffs (drop the board on; self-tap into the pilot or glue)
  translate([0, esp_cy, wall])
    for (sx=[-1,1], sy=[-1,1])
      translate([sx*(esp_l/2 - 4), sy*(esp_w/2 - 4), -eps])
        difference() {
          cylinder(d=esp_post_d, h=esp_standoff + eps);
          cylinder(d=esp_pin_d,  h=esp_standoff + 2);
        }
}

// ---------------------------------------------------------------------------
//  FRONT PLATE  (display window + module pocket + corner screws)
//  Outer/visible face at z=0; inner face (toward shell) at z=front_t.
// ---------------------------------------------------------------------------
module front() {
  difference() {
    rbox(out_x, out_y, front_t, fillet);
    // window straight through the faceplate
    translate([win_dx, win_dy, -1])
      linear_extrude(front_t + 2) square([win_w, win_h], center=true);
    // module pocket cut from the inner face (depth = disp_t)
    translate([0, 0, win_lip])
      linear_extrude(disp_t + 1) square([disp_w+tol, disp_h+tol], center=true);
    // corner clearance holes + countersinks (heads on the visible face)
    for (sx=[-1,1], sy=[-1,1])
      translate([sx*px, sy*py, 0]) {
        translate([0,0,-1]) cylinder(d=scr_clear, h=front_t + 2);
        cylinder(d1=scr_head, d2=scr_clear, h=1.6);
      }
  }
}

// ---------------------------------------------------------------------------
//  Output
// ---------------------------------------------------------------------------
if (part == "shell") shell();
else if (part == "front") front();
else {                                  // "all": preview both, front opened up
  shell();
  translate([0, out_y + 8, front_t]) rotate([0,180,0]) color("LightSteelBlue") front();
}
