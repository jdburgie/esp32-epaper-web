// ============================================================================
//  ESP32 e-paper weather display — refined parametric enclosure
//  Three Oak Woods · esp32-epaper-web
//
//  This edition preserves the original INTERNAL cavity, component locations,
//  port locations, standoffs, battery cradle, screw centres, and display pocket.
//  Only the exterior treatment has been redesigned.
//
//  New exterior treatment:
//    • softly beveled rear shell edge
//    • subtle wraparound accent band near the front seam
//    • tapered/chamfered faceplate perimeter
//    • shallow recessed display panel
//    • beveled display-window opening
//    • softened countersink surrounds
//
//  Units: millimetres.
// ============================================================================

part = "all";          // "all" | "shell" | "front"

/* [Global] */
wall   = 2.0;          // wall / floor thickness
// NOTE: cavity dimensions and all internal geometry remain based on this value.
tol    = 0.4;          // clearance for press-fits
fillet = 3.0;          // legacy/internal corner-radius basis
eps    = 0.1;          // tiny overlap to avoid coincident faces
$fn    = 72;

/* [Exterior styling] */
body_radius       = 4.0;   // larger, softer external corner radius
rear_bevel        = 1.4;   // depth of bevel around rear shell face
rear_inset        = 1.0;   // amount rear face steps inward on each side
accent_enable     = true;  // subtle wraparound band on shell
accent_outset     = 0.55;  // projection of accent band per side
accent_height     = 1.15;  // Z height of accent band
accent_from_front = 2.2;   // distance behind shell/front seam
front_chamfer     = 1.15;  // faceplate perimeter chamfer depth
front_inset       = 0.65;  // visible face inset per side
panel_recess      = 0.45;  // shallow display-surround recess
panel_margin_x    = 4.2;   // recess margin around visible window
panel_margin_y    = 4.0;
panel_radius      = 3.0;
window_bevel      = 0.75;  // bevel width around display opening
window_bevel_z    = 0.70;  // bevel depth
screw_dish_extra  = 0.5;   // soft circular relief around each countersink
screw_dish_depth  = 0.28;

/* [E-paper display module] */
disp_w  = 76.5;        // module outline width  (X)
disp_h  = 34.5;        // module outline height (Y)
disp_t  = 2.4;         // module thickness seated in the front pocket
win_w   = 59.2;        // visible active-area window width
win_h   = 29.3;        // visible active-area window height
win_dx  = -1.0;         // window offset from module centre (X)
win_dy  = 0.0;         // window offset from module centre (Y)
win_lip = 1.8;         // faceplate thickness around the window (retains module)

/* [ESP32 DevKit V1 (30-pin)] */
esp_l        = 52.0;   // PCB length (X)
esp_w        = 28.5;   // PCB width  (Y)
esp_h        = 13.0;   // height incl. headers (Z clearance)
esp_standoff = 4.0;   // standoff post height under the PCB
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
btn_d   = 12.0;        // cycle-button hole dia on the TOP wall (0 = omit)

/* [Mounting] */
wall_mount = false;    // true = add 2 keyhole slots in the back wall
km_dx      = 28.0;     // keyhole spacing from centre (X)
km_y       = 0.0;      // keyhole height offset (Y) — nudge clear of components
stand_tilt = 15;       // easel lean-back angle for the `stand` part

/* [Corner screws] */
scr_d      = 2.6;      // self-tap shaft into the posts (M2.5/M3 self-tapping)
scr_clear  = 3.2;      // clearance hole through the front plate
scr_head   = 6.0;      // countersink dia
post_d     = 7.0;      // corner post diameter

// ---------------------------------------------------------------------------
//  Derived cavity / outer dimensions — unchanged from the original design
// ---------------------------------------------------------------------------
margin  = 3.0;
inner_x = max(batt_l, esp_l, disp_w, chg_l) + 2*margin;
inner_y = batt_w + esp_w + 3*margin;
inner_z = max(cell_d, esp_h) + 2;

out_x   = inner_x + 2*wall;
out_y   = inner_y + 2*wall;
shell_z = inner_z + wall;
front_t = win_lip + disp_t;

batt_cy = -inner_y/2 + batt_w/2 + margin;
esp_cy  =  inner_y/2 - esp_w/2  - margin;

px = inner_x/2 - 2;
py = inner_y/2 - 2;

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
module rr2d(x, y, r) {
  // Rounded rectangle with predictable outside dimensions.
  offset(r) offset(-r) square([x, y], center=true);
}

module rbox(x, y, z, r) {
  linear_extrude(z) rr2d(x, y, r);
}

module hull_slice(x, y, r, z, h=0.12) {
  translate([0,0,z]) linear_extrude(h) rr2d(x, y, r);
}

// Shell exterior: smaller back face flows into the full mating footprint.
module sculpted_shell_outer() {
  union() {
    hull() {
      hull_slice(out_x - 2*rear_inset,
                 out_y - 2*rear_inset,
                 max(1.0, body_radius - rear_inset),
                 0);
      hull_slice(out_x, out_y, body_radius, rear_bevel);
    }
    translate([0,0,rear_bevel])
      rbox(out_x, out_y, shell_z - rear_bevel, body_radius);
  }
}

// Faceplate exterior: exact mating footprint on the inside, gently inset face.
module sculpted_front_outer() {
  union() {
    hull() {
      hull_slice(out_x - 2*front_inset,
                 out_y - 2*front_inset,
                 max(1.0, body_radius - front_inset),
                 0);
      hull_slice(out_x, out_y, body_radius, front_chamfer);
    }
    translate([0,0,front_chamfer])
      rbox(out_x, out_y, front_t - front_chamfer, body_radius);
  }
}

module accent_ring() {
  ring_z = shell_z - accent_from_front - accent_height;
  difference() {
    translate([0,0,ring_z])
      rbox(out_x + 2*accent_outset,
           out_y + 2*accent_outset,
           accent_height,
           body_radius + accent_outset);
    // Preserve the original cavity exactly.
    translate([0,0,ring_z-eps])
      rbox(inner_x, inner_y, accent_height + 2*eps,
           max(0.1, fillet-wall));
  }
}

module beveled_window_cut() {
  // Larger at the visible face, tapering to the exact original opening.
  translate([win_dx, win_dy, panel_recess-eps])
    linear_extrude(height=window_bevel_z + eps,
                   scale=[win_w/(win_w + 2*window_bevel),
                          win_h/(win_h + 2*window_bevel)])
      square([win_w + 2*window_bevel,
              win_h + 2*window_bevel], center=true);

  // Exact original visible-window dimensions through the remaining plate.
  translate([win_dx, win_dy, panel_recess + window_bevel_z-eps])
    linear_extrude(front_t + 2)
      square([win_w, win_h], center=true);
}

module display_panel_recess() {
  translate([win_dx, win_dy, -eps])
    linear_extrude(panel_recess + eps)
      rr2d(win_w + 2*panel_margin_x,
           win_h + 2*panel_margin_y,
           panel_radius);
}

module screw_face_relief() {
  for (sx=[-1,1], sy=[-1,1])
    translate([sx*px, sy*py, -eps])
      cylinder(d=scr_head + screw_dish_extra,
               h=screw_dish_depth + eps);
}

// Wall-mount keyhole: head hole + a slot the screw shank slides up into.
// Cut through the back wall. Head sits inside the cavity — keep km_y clear
// of the battery/ESP, or just use the `stand` part.
module keyhole() {
  union() {
    cylinder(d=8, h=wall*3, center=true);                               // head
    hull() { cylinder(d=4, h=wall*3, center=true);
             translate([0,8,0]) cylinder(d=4, h=wall*3, center=true); } // shank slot
  }
}

// Desk easel: the assembled case drops into a channel and leans back stand_tilt deg.
module stand() {
  case_z = shell_z + front_t;           // assembled depth held by the channel
  d      = stand_tilt + case_z + 22;    // foot depth on the desk
  rest_h = case_z + 16;
  lip_y  = 8 + case_z + 4;
  translate([-out_x/2, -d/2, 0]) {
    cube([out_x, d, wall*2]);                                  // foot
    translate([0, lip_y, 0]) cube([out_x, wall*2, 9]);         // front lip
    translate([0, 8, 0]) rotate([stand_tilt,0,0])              // angled back rest
      cube([out_x, wall*2, rest_h]);
  }
}

// ---------------------------------------------------------------------------
//  SHELL  (same cavity and internals; refined exterior only)
// ---------------------------------------------------------------------------
module shell() {
  union() {
    difference() {
      sculpted_shell_outer();

      // Original cavity, unchanged and open at the front (+Z).
      translate([0,0,wall])
        rbox(inner_x, inner_y, shell_z + 1,
             max(0.1, fillet-wall));

      // Original charge port.
      translate([0, -out_y/2, wall + usb_h/2 + 1])
        rotate([90,0,0]) cube([usb_w, usb_h, wall*4], center=true);

      // Original optional power-switch hole.
      if (sw_d > 0)
        translate([out_x/2, esp_cy, wall + esp_h/2])
          rotate([0,90,0]) cylinder(d=sw_d, h=wall*4, center=true);

      // Cycle-button hole on the top wall (+Y).
      if (btn_d > 0)
        translate([0, out_y/2, wall + inner_z*0.5])
          rotate([90,0,0]) cylinder(d=btn_d, h=wall*4, center=true);

      // Optional wall-mount keyholes in the back wall (z=0).
      if (wall_mount)
        for (sx=[-1,1]) translate([sx*km_dx, km_y, 0]) keyhole();
    }

    if (accent_enable) accent_ring();

    // Original corner posts, unchanged.
    for (sx=[-1,1], sy=[-1,1])
      translate([sx*px, sy*py, 0])
        difference() {
          cylinder(d=post_d, h=shell_z);
          translate([0,0,wall]) cylinder(d=scr_d, h=shell_z);
        }

    // Original 18650 cradle, unchanged.
    for (sx=[-1,1])
      translate([sx*batt_l*0.30, batt_cy, wall])
        difference() {
          translate([-4, -batt_w/2, -eps])
            cube([8, batt_w, cell_d*0.7 + eps]);
          translate([0, 0, cell_d*0.7 + cell_d/2 - 1])
            rotate([90,0,90])
              cylinder(d=cell_d+tol, h=20, center=true);
        }

    // Original ESP32 standoffs, unchanged.
    translate([0, esp_cy, wall])
      for (sx=[-1,1], sy=[-1,1])
        translate([sx*(esp_l/2 - 4), sy*(esp_w/2 - 4), -eps])
          difference() {
            cylinder(d=esp_post_d, h=esp_standoff + eps);
            cylinder(d=esp_pin_d,  h=esp_standoff + 2);
          }
  }
}

// ---------------------------------------------------------------------------
//  FRONT PLATE  (same pocket/window/screws; refined exterior only)
//  Visible face at z=0; inner face toward shell at z=front_t.
// ---------------------------------------------------------------------------
module front() {
  difference() {
    sculpted_front_outer();

    // New shallow visual surround, exterior side only.
    display_panel_recess();

    // Original active-area opening, now with a small entry bevel.
    beveled_window_cut();

    // Original module pocket, unchanged.
    translate([0, 0, win_lip])
      linear_extrude(disp_t + 1)
        square([disp_w+tol, disp_h+tol], center=true);

    // Soft face relief around the original countersinks.
    screw_face_relief();

    // Original screw holes and countersinks, unchanged.
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
else if (part == "stand") stand();
else {
  shell();
  translate([0, out_y + 10, front_t])
    rotate([0,180,0])
      color("LightSteelBlue") front();
}
