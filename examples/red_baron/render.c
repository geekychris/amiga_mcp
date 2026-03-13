/*
 * render.c - 3D wireframe rendering, HUD, title screen for Red Baron
 */
#include <proto/graphics.h>
#include <graphics/gfxbase.h>
#include <string.h>
#include <stdio.h>
#include "render.h"

/* Ground grid parameters */
#define GRID_SPACING 200
#define GRID_EXTENT  8000
#define GRID_LINES   21
#define GROUND_FAR   8000

/* ---- Helper: draw a clipped line in viewport ---- */
static void draw_line(struct RastPort *rp, WORD x1, WORD y1, WORD x2, WORD y2,
                      const Viewport *vp, WORD color)
{
    WORD left = vp->x;
    WORD top  = vp->y;
    WORD right = vp->x + vp->w - 1;
    WORD bottom = vp->y + vp->h - 1;

    if (!clip_line(&x1, &y1, &x2, &y2, left, top, right, bottom))
        return;

    SetAPen(rp, color);
    Move(rp, x1, y1);
    Draw(rp, x2, y2);
}

/* ---- Render a wireframe model at a world position ---- */
static void render_model(struct RastPort *rp, const Camera *cam,
                         const Viewport *vp, const Model *mdl,
                         LONG wx, LONG wy, LONG wz, WORD yaw,
                         WORD obj_type)
{
    WORD i;
    WORD sx[24], sy[24];  /* max 24 projected vertices */
    WORD vis[24];
    LONG depth = 0;
    WORD color;

    if (mdl->num_verts > 24) return;

    /* Transform and project each vertex */
    for (i = 0; i < mdl->num_verts; i++) {
        LONG mx, my, mz;
        LONG cx, cy, cz;
        LONG vx = mdl->verts[i][0];
        LONG vy = mdl->verts[i][1];
        LONG vz = mdl->verts[i][2];

        /* Rotate model vertex by object yaw */
        rotate_y(vx, vy, vz, yaw, &mx, &my, &mz);

        /* Add world position */
        mx += wx;
        my += wy;
        mz += wz;

        /* Camera transform */
        transform_point(cam, mx, my, mz, &cx, &cy, &cz);

        /* Project */
        vis[i] = project_point(cx, cy, cz, vp, &sx[i], &sy[i]);

        if (i == 0) depth = cz;
    }

    /* Skip if center is behind camera or too far */
    if (depth < NEAR_CLIP || depth > FAR_CLIP) return;

    color = get_obj_color(obj_type, depth);

    /* Draw edges */
    for (i = 0; i < mdl->num_edges; i++) {
        WORD v1 = mdl->edges[i][0];
        WORD v2 = mdl->edges[i][1];
        if (vis[v1] && vis[v2]) {
            draw_line(rp, sx[v1], sy[v1], sx[v2], sy[v2], vp, color);
        }
    }
}

/* ---- Overflow-safe camera transform for ground (large world coords) ---- */
/* Pre-shifts trig values by 8 bits so dx*trig fits in 32 bits for dx up to ~32000.
 * Max dx=32000, trig>>8=256, product=8M, well within LONG range.
 * Result is same scale as normal transform_point (world units). */
static void transform_ground(const Camera *cam, LONG wx, LONG wy, LONG wz,
                             LONG *cx, LONG *cy, LONG *cz)
{
    LONG dx, dy, dz;
    LONG rx, ry, rz;
    LONG cyaw, syaw, cpitch, spitch;

    dx = wx - cam->pos.x;
    dy = wy - cam->pos.y;
    dz = wz - cam->pos.z;

    /* Trig values pre-shifted >>8 (from 16.16 fixed to 8.8 effective) */
    cyaw   = cos_tab[cam->yaw & 255] >> 8;
    syaw   = sin_tab[cam->yaw & 255] >> 8;

    /* Rotate by yaw: result >>8 instead of >>16, same final scale */
    rx = (dx * cyaw - dz * syaw) >> 8;
    rz = (dx * syaw + dz * cyaw) >> 8;
    ry = dy;

    /* Pitch rotation */
    cpitch = cos_tab[cam->pitch & 255] >> 8;
    spitch = sin_tab[cam->pitch & 255] >> 8;
    *cx = rx;
    *cy = (ry * cpitch + rz * spitch) >> 8;
    *cz = (-ry * spitch + rz * cpitch) >> 8;
}

/* ---- Near-clip a 3D line segment, project both endpoints to screen ---- */
/* Returns 1 if any part is in front of camera. Does NOT reject off-screen;
 * that's left to the 2D clipper in draw_line(). */
static WORD clip3d_project_far(const Camera *cam, const Viewport *vp,
                               LONG wx0, LONG wy0, LONG wz0,
                               LONG wx1, LONG wy1, LONG wz1,
                               WORD *sx0, WORD *sy0, WORD *sx1, WORD *sy1,
                               LONG far_clip)
{
    LONG cx0, cy0, cz0, cx1, cy1, cz1;
    LONG px, py;
    WORD hcx = vp->x + vp->w / 2;
    WORD hcy = vp->y + vp->h / 2;

    transform_point(cam, wx0, wy0, wz0, &cx0, &cy0, &cz0);
    transform_point(cam, wx1, wy1, wz1, &cx1, &cy1, &cz1);

    /* Both behind camera? */
    if (cz0 < NEAR_CLIP && cz1 < NEAR_CLIP) return 0;

    /* Clip endpoint 0 to near plane — use div-first to avoid overflow */
    if (cz0 < NEAR_CLIP) {
        LONG dz = cz1 - cz0;
        if (dz != 0) {
            LONG t = NEAR_CLIP - cz0;
            cx0 = cx0 + (cx1 - cx0) / dz * t;
            cy0 = cy0 + (cy1 - cy0) / dz * t;
            cz0 = NEAR_CLIP;
        }
    }
    /* Clip endpoint 1 to near plane */
    if (cz1 < NEAR_CLIP) {
        LONG dz = cz0 - cz1;
        if (dz != 0) {
            LONG t = NEAR_CLIP - cz1;
            cx1 = cx1 + (cx0 - cx1) / dz * t;
            cy1 = cy1 + (cy0 - cy1) / dz * t;
            cz1 = NEAR_CLIP;
        }
    }

    /* Clip to far plane — use >>4 pre-shift to avoid overflow */
    if (cz0 > far_clip && cz1 > far_clip) return 0;
    if (cz0 > far_clip) {
        LONG t = (far_clip - cz1) >> 4;
        LONG dz = (cz0 - cz1) >> 4;
        if (dz != 0) {
            cx0 = cx1 + (cx0 - cx1) / dz * t;
            cy0 = cy1 + (cy0 - cy1) / dz * t;
            cz0 = far_clip;
        }
    }
    if (cz1 > far_clip) {
        LONG t = (far_clip - cz0) >> 4;
        LONG dz = (cz1 - cz0) >> 4;
        if (dz != 0) {
            cx1 = cx0 + (cx1 - cx0) / dz * t;
            cy1 = cy0 + (cy1 - cy0) / dz * t;
            cz1 = far_clip;
        }
    }

    /* Project — inline to allow large off-screen values (2D clipper handles it) */
    if (cz0 < NEAR_CLIP) cz0 = NEAR_CLIP;
    if (cz1 < NEAR_CLIP) cz1 = NEAR_CLIP;

    px = (LONG)hcx + (cx0 * FOV_SCALE) / cz0;
    py = (LONG)hcy - (cy0 * FOV_SCALE) / cz0;
    /* Clamp to WORD range to avoid overflow in clipper */
    if (px < -2000) px = -2000; if (px > 2000) px = 2000;
    if (py < -2000) py = -2000; if (py > 2000) py = 2000;
    *sx0 = (WORD)px;
    *sy0 = (WORD)py;

    px = (LONG)hcx + (cx1 * FOV_SCALE) / cz1;
    py = (LONG)hcy - (cy1 * FOV_SCALE) / cz1;
    if (px < -2000) px = -2000; if (px > 2000) px = 2000;
    if (py < -2000) py = -2000; if (py > 2000) py = 2000;
    *sx1 = (WORD)px;
    *sy1 = (WORD)py;

    return 1;
}

/* ---- Render horizon line ---- */
static void render_horizon(struct RastPort *rp, const Camera *cam,
                           const Viewport *vp)
{
    /*
     * Compute horizon analytically. The ground plane is at y=GROUND_Y.
     * The horizon screen-Y depends on camera height above ground and pitch.
     * Height above ground in camera space (after pitch rotation):
     *   dy = GROUND_Y - cam->pos.y  (negative when above ground)
     *   After pitch: the Y/Z ratio at infinity converges, giving us a
     *   fixed screen-Y for the horizon line.
     *
     * For a flat ground plane, the horizon is a perfectly horizontal line
     * when there's no roll. We compute it by finding the screen-Y where
     * a ground point at infinite Z would project to.
     */
    LONG dy = GROUND_Y - cam->pos.y;  /* negative = above ground */
    Fixed cpitch = cos_tab[cam->pitch & 255];
    Fixed spitch = sin_tab[cam->pitch & 255];
    LONG horizon_cy, horizon_cz;
    LONG horizon_sy;
    WORD hcy = vp->y + vp->h / 2;
    WORD hy;
    WORD color = (g_tune.display_mode == DISPLAY_CLASSIC) ? 2 : 13;

    /*
     * At infinite distance along camera Z, a point on the ground has:
     *   camera_y = dy * cpitch + inf * spitch  -> dominated by spitch at infinity
     *   camera_z = -dy * spitch + inf * cpitch -> dominated by cpitch at infinity
     *
     * But the ratio cy/cz converges to:
     *   For the ground plane component: (dy * cpitch) / (-dy * spitch + large_z)
     *   As z->inf: approaches 0 if cpitch != 0
     *
     * Actually, for a point at (0, GROUND_Y, very_far_z) relative to camera:
     *   After yaw: dx=0, dy=dy, dz=very_far
     *   After pitch: cy = dy*cpitch + dz*spitch, cz = -dy*spitch + dz*cpitch
     *   cy/cz = (dy*cpitch + dz*spitch) / (-dy*spitch + dz*cpitch)
     *   As dz -> infinity: ratio -> spitch/cpitch = tan(pitch)
     *   But the ground contribution: at infinity, screen_y -> hcy - tan(pitch)*FOV
     *
     * For the ground plane specifically at finite distance:
     *   Use dz = 20000 as "practically infinity"
     */
    horizon_cy = (dy * (cpitch >> 8) >> 8) + (20000L * (spitch >> 8) >> 8);
    horizon_cz = (-(dy * (spitch >> 8) >> 8)) + (20000L * (cpitch >> 8) >> 8);

    if (horizon_cz < 1) horizon_cz = 1;

    horizon_sy = (LONG)hcy - (horizon_cy * FOV_SCALE) / horizon_cz;

    /* Clamp to viewport range */
    hy = (WORD)horizon_sy;
    if (hy < vp->y - 1) hy = vp->y - 1;
    if (hy > vp->y + vp->h) hy = vp->y + vp->h;

    /* Draw full-width horizon line */
    if (hy >= vp->y && hy < vp->y + vp->h) {
        SetAPen(rp, color);
        Move(rp, vp->x, hy);
        Draw(rp, vp->x + vp->w - 1, hy);
    }
}

/* ---- Render ground grid (camera-relative) ---- */
/* Grid is drawn relative to camera direction so it always looks like a
 * proper checkerboard pattern. Ground targets still rotate around the
 * player giving the sense of turning. This matches the original arcade. */
static void render_ground(struct RastPort *rp, const Camera *cam,
                          const Viewport *vp)
{
    WORD i;
    WORD color = get_obj_color(OBJ_GRID, 500);
    WORD hcx = vp->x + vp->w / 2;
    WORD hcy = vp->y + vp->h / 2;

    /* Horizon line first */
    render_horizon(rp, cam, vp);

    /*
     * Camera-relative grid: lines go forward (camera Z) and sideways (camera X).
     * We work directly in camera space, skipping the world->camera transform.
     *
     * Forward lines: at camera-space x = (i - half) * SPACING, from cz=NEAR_CLIP to cz=FAR
     * Sideways lines: at camera-space z = j * SPACING, from cx=-EXTENT to cx=+EXTENT
     *
     * The camera height above ground determines the camera-space Y of the ground:
     *   ground_cy = GROUND_Y - cam->pos.y (negative when above ground)
     * After pitch rotation, this shifts the projected Y, but for simplicity
     * we project ground lines at the camera-relative ground height.
     */
    {
        LONG ground_dy = GROUND_Y - cam->pos.y;  /* e.g. -300 */
        Fixed cpitch = cos_tab[cam->pitch & 255];
        Fixed spitch = sin_tab[cam->pitch & 255];
        Fixed cyaw = cos_tab[cam->yaw & 255];
        Fixed syaw = sin_tab[cam->yaw & 255];

        /*
         * Compute camera's world position projected onto camera axes:
         * lateral = dot(cam_pos, cam_right) = cam_x*cos_yaw - cam_z*sin_yaw
         * forward = dot(cam_pos, cam_fwd)   = cam_x*sin_yaw + cam_z*cos_yaw
         * Use these to offset grid lines so they scroll with world movement/rotation.
         */
        LONG lat_offset = (cam->pos.x * (cyaw >> 8) - cam->pos.z * (syaw >> 8)) >> 8;
        LONG fwd_offset = (cam->pos.x * (syaw >> 8) + cam->pos.z * (cyaw >> 8)) >> 8;

        /* Snap offsets to grid spacing to get the shift */
        LONG lat_snap = lat_offset / GRID_SPACING * GRID_SPACING;
        LONG fwd_snap = fwd_offset / GRID_SPACING * GRID_SPACING;
        LONG lat_shift = lat_offset - lat_snap;  /* remainder: 0..GRID_SPACING-1 */
        LONG fwd_shift = fwd_offset - fwd_snap;

        /* Pre-compute pitch-adjusted Z at near and far for forward lines */
        LONG acz_near = (-ground_dy * (spitch >> 8) + (LONG)NEAR_CLIP * (cpitch >> 8)) >> 8;
        LONG acy_near = (ground_dy * (cpitch >> 8) + (LONG)NEAR_CLIP * (spitch >> 8)) >> 8;
        LONG acz_far  = (-ground_dy * (spitch >> 8) + (LONG)GROUND_FAR * (cpitch >> 8)) >> 8;
        LONG acy_far  = (ground_dy * (cpitch >> 8) + (LONG)GROUND_FAR * (spitch >> 8)) >> 8;

        /* Safety: skip ground rendering if pitch makes it invisible */
        if (acz_near < 1) acz_near = 1;
        if (acz_far < 1) acz_far = acz_near;

        /* Forward lines (parallel to camera Z axis) */
        for (i = 0; i < GRID_LINES; i++) {
            LONG cx = (LONG)(i - GRID_LINES / 2) * GRID_SPACING - lat_shift;
            LONG px0, py0, px1, py1;
            WORD sx0, sy0, sx1, sy1;

            px0 = (LONG)hcx + cx * FOV_SCALE / acz_near;
            py0 = (LONG)hcy - acy_near * FOV_SCALE / acz_near;
            px1 = (LONG)hcx + cx * FOV_SCALE / acz_far;
            py1 = (LONG)hcy - acy_far * FOV_SCALE / acz_far;

            if (px0 < -2000) px0 = -2000; if (px0 > 2000) px0 = 2000;
            if (py0 < -2000) py0 = -2000; if (py0 > 2000) py0 = 2000;
            if (px1 < -2000) px1 = -2000; if (px1 > 2000) px1 = 2000;
            if (py1 < -2000) py1 = -2000; if (py1 > 2000) py1 = 2000;

            sx0 = (WORD)px0; sy0 = (WORD)py0;
            sx1 = (WORD)px1; sy1 = (WORD)py1;
            draw_line(rp, sx0, sy0, sx1, sy1, vp, color);
        }

        /* Sideways lines (perpendicular, shifted by forward world position) */
        {
            LONG cx_left  = -(LONG)(GRID_LINES / 2) * GRID_SPACING;
            LONG cx_right =  (LONG)(GRID_LINES / 2) * GRID_SPACING;

            for (i = 1; i <= GRID_LINES; i++) {
                LONG cz = (LONG)i * GRID_SPACING - fwd_shift;
                LONG acz, acy;
                LONG px0, py0, px1;
                WORD sx0, sy0, sx1;

                if (cz < 1) continue;

                acz = (-ground_dy * (spitch >> 8) + cz * (cpitch >> 8)) >> 8;
                acy = (ground_dy * (cpitch >> 8) + cz * (spitch >> 8)) >> 8;

                if (acz < 1) continue;

                px0 = (LONG)hcx + cx_left * FOV_SCALE / acz;
                py0 = (LONG)hcy - acy * FOV_SCALE / acz;
                px1 = (LONG)hcx + cx_right * FOV_SCALE / acz;

                if (px0 < -2000) px0 = -2000; if (px0 > 2000) px0 = 2000;
                if (py0 < -2000) py0 = -2000; if (py0 > 2000) py0 = 2000;
                if (px1 < -2000) px1 = -2000; if (px1 > 2000) px1 = 2000;

                sx0 = (WORD)px0; sy0 = (WORD)py0;
                sx1 = (WORD)px1;
                draw_line(rp, sx0, sy0, sx1, sy0, vp, color);
            }
        }
    }
}

/* ---- Render bullets ---- */
static void render_bullets(struct RastPort *rp, const Camera *cam,
                           const Viewport *vp, Bullet *bullets)
{
    WORD i;
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &bullets[i];
        LONG cx, cy, cz;
        WORD sx, sy;
        WORD color;
        if (!b->active) continue;

        transform_point(cam, b->pos.x, b->pos.y, b->pos.z, &cx, &cy, &cz);
        if (project_point(cx, cy, cz, vp, &sx, &sy)) {
            /* Also project the tail (one frame behind) for tracer effect */
            LONG tx, ty, tz;
            WORD sx2, sy2;
            color = get_obj_color(b->owner == 2 ? OBJ_BULLET_E : OBJ_BULLET_P, cz);
            transform_point(cam, b->pos.x - b->vx, b->pos.y - b->vy,
                           b->pos.z - b->vz, &tx, &ty, &tz);
            if (project_point(tx, ty, tz, vp, &sx2, &sy2)) {
                draw_line(rp, sx, sy, sx2, sy2, vp, color);
            } else if (sx >= vp->x && sx < vp->x + vp->w - 1 &&
                       sy >= vp->y && sy < vp->y + vp->h - 1) {
                SetAPen(rp, color);
                RectFill(rp, sx, sy, sx + 1, sy + 1);
            }
        }
    }
}

/* ---- Render particles ---- */
static void render_particles(struct RastPort *rp, const Camera *cam,
                             const Viewport *vp, Particle *particles)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &particles[i];
        LONG cx, cy, cz;
        WORD sx, sy;
        WORD color;
        if (!p->active) continue;

        transform_point(cam, p->pos.x, p->pos.y, p->pos.z, &cx, &cy, &cz);
        if (project_point(cx, cy, cz, vp, &sx, &sy)) {
            if (g_tune.display_mode == DISPLAY_CLASSIC)
                color = (p->life > 10) ? 2 : 4;
            else
                color = (p->life > 10) ? 9 : 14;  /* orange / bright orange */

            if (sx >= vp->x && sx < vp->x + vp->w &&
                sy >= vp->y && sy < vp->y + vp->h) {
                SetAPen(rp, color);
                WritePixel(rp, sx, sy);
            }
        }
    }
}

/* ---- Render crosshair ---- */
static void render_crosshair(struct RastPort *rp, const Viewport *vp)
{
    WORD cx = vp->x + vp->w / 2;
    WORD cy = vp->y + vp->h / 2;
    WORD color = (g_tune.display_mode == DISPLAY_CLASSIC) ? 2 : 1;

    SetAPen(rp, color);
    /* Horizontal */
    Move(rp, cx - 12, cy);
    Draw(rp, cx - 4, cy);
    Move(rp, cx + 4, cy);
    Draw(rp, cx + 12, cy);
    /* Vertical */
    Move(rp, cx, cy - 12);
    Draw(rp, cx, cy - 4);
    Move(rp, cx, cy + 4);
    Draw(rp, cx, cy + 12);
    /* Center dot */
    WritePixel(rp, cx, cy);
}

/* ---- Full scene render for one player ---- */
void render_scene(struct RastPort *rp, GameWorld *w, WORD player_idx,
                  const Viewport *vp)
{
    Camera *cam = &w->players[player_idx].cam;
    WORD i;

    /* Clear viewport */
    SetAPen(rp, 0);
    RectFill(rp, vp->x, vp->y, vp->x + vp->w - 1, vp->y + vp->h - 1);

    /* Ground grid */
    render_ground(rp, cam, vp);

    /* Ground targets */
    for (i = 0; i < MAX_GROUND; i++) {
        GroundTarget *g = &w->ground[i];
        if (!g->active) continue;
        render_model(rp, cam, vp, &models[g->model_id],
                     g->pos.x, g->pos.y, g->pos.z, g->yaw, OBJ_GROUND);
    }

    /* Enemies */
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &w->enemies[i];
        WORD ot;
        if (!e->active) continue;
        ot = (e->model_id == MODEL_BLIMP) ? OBJ_BLIMP : OBJ_ENEMY;
        render_model(rp, cam, vp, &models[e->model_id],
                     e->pos.x, e->pos.y, e->pos.z, e->yaw, ot);
    }

    /* Bullets */
    render_bullets(rp, cam, vp, w->bullets);

    /* Particles */
    render_particles(rp, cam, vp, w->particles);

    /* Crosshair */
    render_crosshair(rp, vp);
}

/* ---- HUD ---- */
void render_hud(struct RastPort *rp, GameWorld *w, WORD player_idx,
                const Viewport *vp)
{
    PlayerState *p = &w->players[player_idx];
    char buf[40];
    WORD hx = vp->x + 4;
    WORD hy = vp->y + 10;
    WORD color = (g_tune.display_mode == DISPLAY_CLASSIC) ? 2 : 10;  /* green or yellow */
    WORD i;

    SetAPen(rp, color);
    SetBPen(rp, 0);

    /* Score */
    sprintf(buf, "%07ld", (long)p->score);
    Move(rp, hx, hy);
    Text(rp, buf, strlen(buf));

    /* Lives (small triangles) */
    for (i = 0; i < p->lives && i < 5; i++) {
        WORD lx = hx + i * 12;
        WORD ly = hy + 8;
        SetAPen(rp, color);
        Move(rp, lx + 4, ly);
        Draw(rp, lx, ly + 3);
        Draw(rp, lx, ly - 3);
        Draw(rp, lx + 4, ly);
    }

    /* Wave number */
    sprintf(buf, "W%ld", (long)w->wave);
    Move(rp, vp->x + vp->w - 40, hy);
    SetAPen(rp, color);
    Text(rp, buf, strlen(buf));

    /* Altitude */
    sprintf(buf, "ALT:%ld", (long)p->cam.pos.y);
    Move(rp, vp->x + vp->w - 80, hy + 12);
    Text(rp, buf, strlen(buf));

    /* Speed */
    sprintf(buf, "SPD:%ld", (long)p->speed);
    Move(rp, vp->x + vp->w - 80, hy + 22);
    Text(rp, buf, strlen(buf));

    /* Player label for split screen */
    if (g_tune.num_players >= 2) {
        sprintf(buf, "P%ld", (long)(player_idx + 1));
        Move(rp, vp->x + vp->w / 2 - 8, hy);
        Text(rp, buf, strlen(buf));
    }

    /* Dead indicator */
    if (!p->alive && p->lives > 0) {
        SetAPen(rp, 8);
        Move(rp, vp->x + vp->w / 2 - 32, vp->y + vp->h / 2);
        Text(rp, "GET READY", 9);
    }
}

/* ---- Split screen divider ---- */
void render_divider(struct RastPort *rp)
{
    SetAPen(rp, 7);
    Move(rp, 0, SCREEN_H / 2);
    Draw(rp, SCREEN_W - 1, SCREEN_H / 2);
}

/* ---- 5x7 bitmap font for title ---- */
static const UBYTE font5x7[][5] = {
    /* A */ {0x7E,0x11,0x11,0x11,0x7E},
    /* B */ {0x7F,0x49,0x49,0x49,0x36},
    /* C */ {0x3E,0x41,0x41,0x41,0x22},
    /* D */ {0x7F,0x41,0x41,0x41,0x3E},
    /* E */ {0x7F,0x49,0x49,0x49,0x41},
    /* F */ {0x7F,0x09,0x09,0x09,0x01},
    /* G */ {0x3E,0x41,0x49,0x49,0x3A},
    /* H */ {0x7F,0x08,0x08,0x08,0x7F},
    /* I */ {0x00,0x41,0x7F,0x41,0x00},
    /* J */ {0x20,0x40,0x41,0x3F,0x01},
    /* K */ {0x7F,0x08,0x14,0x22,0x41},
    /* L */ {0x7F,0x40,0x40,0x40,0x40},
    /* M */ {0x7F,0x02,0x0C,0x02,0x7F},
    /* N */ {0x7F,0x04,0x08,0x10,0x7F},
    /* O */ {0x3E,0x41,0x41,0x41,0x3E},
    /* P */ {0x7F,0x09,0x09,0x09,0x06},
    /* Q */ {0x3E,0x41,0x51,0x21,0x5E},
    /* R */ {0x7F,0x09,0x19,0x29,0x46},
    /* S */ {0x46,0x49,0x49,0x49,0x31},
    /* T */ {0x01,0x01,0x7F,0x01,0x01},
    /* U */ {0x3F,0x40,0x40,0x40,0x3F},
    /* V */ {0x1F,0x20,0x40,0x20,0x1F},
    /* W */ {0x3F,0x40,0x30,0x40,0x3F},
    /* X */ {0x63,0x14,0x08,0x14,0x63},
    /* Y */ {0x07,0x08,0x70,0x08,0x07},
    /* Z */ {0x61,0x51,0x49,0x45,0x43},
};

/* Digit font 0-9 */
static const UBYTE font_digit[][5] = {
    /* 0 */ {0x3E,0x51,0x49,0x45,0x3E},
    /* 1 */ {0x00,0x42,0x7F,0x40,0x00},
    /* 2 */ {0x42,0x61,0x51,0x49,0x46},
    /* 3 */ {0x22,0x41,0x49,0x49,0x36},
    /* 4 */ {0x18,0x14,0x12,0x7F,0x10},
    /* 5 */ {0x27,0x45,0x45,0x45,0x39},
    /* 6 */ {0x3C,0x4A,0x49,0x49,0x30},
    /* 7 */ {0x01,0x71,0x09,0x05,0x03},
    /* 8 */ {0x36,0x49,0x49,0x49,0x36},
    /* 9 */ {0x06,0x49,0x49,0x29,0x1E},
};

static void draw_big_char(struct RastPort *rp, char ch, WORD x, WORD y, WORD scale)
{
    const UBYTE *glyph = NULL;
    WORD col, row;

    if (ch >= 'A' && ch <= 'Z')      glyph = font5x7[ch - 'A'];
    else if (ch >= 'a' && ch <= 'z')  glyph = font5x7[ch - 'a'];
    else if (ch >= '0' && ch <= '9')  glyph = font_digit[ch - '0'];
    else return;

    for (col = 0; col < 5; col++) {
        UBYTE bits = glyph[col];
        for (row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                WORD px = x + col * scale;
                WORD py = y + row * scale;
                RectFill(rp, px, py, px + scale - 1, py + scale - 1);
            }
        }
    }
}

static WORD big_string_width(const char *str, WORD scale)
{
    WORD w = 0;
    while (*str) {
        if (*str == ' ') w += scale * 4;
        else w += scale * 6;
        str++;
    }
    return w > 0 ? w - scale : 0;
}

static void draw_big_string(struct RastPort *rp, const char *str,
                            WORD x, WORD y, WORD scale)
{
    while (*str) {
        if (*str == ' ') {
            x += scale * 4;
        } else {
            draw_big_char(rp, *str, x, y, scale);
            x += scale * 6;
        }
        str++;
    }
}

/* ---- Title screen ---- */
void render_title(struct RastPort *rp)
{
    WORD cx = SCREEN_W / 2;
    WORD w;

    /* Clear */
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    /* Decorative star field */
    {
        ULONG seed = 4242;
        WORD i;
        SetAPen(rp, 13);
        for (i = 0; i < 40; i++) {
            WORD sx, sy;
            seed = seed * 1103515245UL + 12345UL;
            sx = (WORD)((seed >> 16) % SCREEN_W);
            sy = (WORD)((seed >> 3) % SCREEN_H);
            WritePixel(rp, sx, sy);
        }
    }

    /* Title */
    SetAPen(rp, 8);  /* red */
    w = big_string_width("RED", 5);
    draw_big_string(rp, "RED", cx - w / 2, 30, 5);

    SetAPen(rp, 1);  /* white */
    w = big_string_width("BARON", 5);
    draw_big_string(rp, "BARON", cx - w / 2, 72, 5);

    /* Decorative lines */
    SetAPen(rp, 8);
    Move(rp, 30, 115);
    Draw(rp, SCREEN_W - 30, 115);
    SetAPen(rp, 6);
    Move(rp, 30, 117);
    Draw(rp, SCREEN_W - 30, 117);

    /* Subtitle */
    SetAPen(rp, 10);  /* yellow */
    w = big_string_width("ACE OF THE SKIES", 2);
    draw_big_string(rp, "ACE OF THE SKIES", cx - w / 2, 126, 2);

    /* Controls */
    SetAPen(rp, 7);
    SetBPen(rp, 0);
    Move(rp, 40, 155);
    Text(rp, "JOYSTICK OR ARROWS  FLY", 23);
    Move(rp, 40, 167);
    Text(rp, "SPACE/FIRE BUTTON   SHOOT", 25);
    Move(rp, 40, 179);
    Text(rp, "Q/E  THROTTLE UP/DOWN", 21);
    Move(rp, 40, 191);
    Text(rp, "P  TOGGLE DISPLAY MODE", 22);

    /* Mode selection */
    SetAPen(rp, 3);  /* cyan */
    Move(rp, 50, 212);
    Text(rp, "F1  1 PLAYER    F2  2 PLAYER", 28);

    /* "PRESS FIRE TO START" */
    SetAPen(rp, 15);
    {
        WORD pw;
        static const char *prompt = "PRESS FIRE TO START";
        pw = 19 * 8;
        Move(rp, cx - pw / 2, 240);
        Text(rp, prompt, 19);
    }
}

/* ---- Game over screen ---- */
void render_gameover(struct RastPort *rp, GameWorld *w)
{
    WORD cx = SCREEN_W / 2;
    WORD wy;
    char buf[40];

    SetAPen(rp, 8);  /* red */
    wy = big_string_width("GAME OVER", 4);
    draw_big_string(rp, "GAME OVER", cx - wy / 2, SCREEN_H / 2 - 40, 4);

    SetAPen(rp, 1);
    SetBPen(rp, 0);
    sprintf(buf, "P1 SCORE  %ld", (long)w->players[0].score);
    Move(rp, cx - (WORD)(strlen(buf) * 4), SCREEN_H / 2 + 4);
    Text(rp, buf, strlen(buf));

    if (g_tune.num_players >= 2) {
        sprintf(buf, "P2 SCORE  %ld", (long)w->players[1].score);
        Move(rp, cx - (WORD)(strlen(buf) * 4), SCREEN_H / 2 + 16);
        Text(rp, buf, strlen(buf));
    }

    SetAPen(rp, 15);
    Move(rp, cx - 40, SCREEN_H / 2 + 36);
    Text(rp, "PRESS FIRE", 10);
}
