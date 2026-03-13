/*
 * RJ and Dale's Super Bros - Player Logic
 * Physics, collision, animation, rendering
 */
#include "game.h"

void player_init(Player *p, int character, int start_x, int start_y) {
    p->x = start_x;
    p->y = start_y;
    p->vx = 0;
    p->vy = 0;
    p->dir = DIR_RIGHT;
    p->on_ground = 0;
    p->character = character;
    p->health = 3;
    p->max_health = 5;
    p->lives = 3;
    p->score = 0;
    p->state = 0;
    p->frame = 0;
    p->frame_timer = 0;
    p->invuln = 0;
    p->speed_boost = 0;
    p->walk_frame = 0;
}

/* Check if a point in the world is solid */
static BOOL point_solid(int wx, int wy) {
    int tx = wx / TILE_SIZE;
    int ty = wy / TILE_SIZE;
    return level_is_solid(tx, ty);
}

/* Check if standing on a platform */
static BOOL on_platform(int wx, int wy) {
    int tx = wx / TILE_SIZE;
    int ty = wy / TILE_SIZE;
    return level_is_platform(tx, ty);
}

void player_update(Player *p, UWORD input) {
    int ph = (p->character == CHAR_RJ) ? PLAYER_H : PLAYER_H_SHORT;
    int speed = MOVE_SPEED;
    int new_x, new_y;
    int top_y, bot_y;
    int tx, ty;

    if (p->speed_boost > 0) {
        speed = MOVE_SPEED + FP(1);
        p->speed_boost--;
    }
    if (p->invuln > 0) p->invuln--;

    /* Horizontal movement */
    p->vx = 0;
    if (input & INP_LEFT) {
        p->vx = -speed;
        p->dir = DIR_LEFT;
    }
    if (input & INP_RIGHT) {
        p->vx = speed;
        p->dir = DIR_RIGHT;
    }

    /* Jump */
    if ((input & INP_JUMP) && p->on_ground) {
        p->vy = JUMP_FORCE;
        p->on_ground = 0;
        sound_jump();
    }

    /* Gravity */
    p->vy += GRAVITY;
    if (p->vy > MAX_FALL) p->vy = MAX_FALL;

    /* Horizontal collision */
    new_x = p->x + FP_INT(p->vx);
    top_y = p->y + 2;
    bot_y = p->y + ph - 2;

    if (new_x < 0) new_x = 0;
    if (new_x + PLAYER_W > level_width_pixels())
        new_x = level_width_pixels() - PLAYER_W;

    /* Check left/right edges against solid tiles */
    if (p->vx < 0) {
        if (point_solid(new_x + 1, top_y) || point_solid(new_x + 1, bot_y) ||
            point_solid(new_x + 1, p->y + ph / 2)) {
            tx = (new_x + 1) / TILE_SIZE;
            new_x = (tx + 1) * TILE_SIZE - 1;
        }
    } else if (p->vx > 0) {
        if (point_solid(new_x + PLAYER_W - 1, top_y) ||
            point_solid(new_x + PLAYER_W - 1, bot_y) ||
            point_solid(new_x + PLAYER_W - 1, p->y + ph / 2)) {
            tx = (new_x + PLAYER_W - 1) / TILE_SIZE;
            new_x = tx * TILE_SIZE - PLAYER_W;
        }
    }
    p->x = new_x;

    /* Vertical collision */
    new_y = p->y + FP_INT(p->vy);

    if (p->vy < 0) {
        /* Moving up - check head */
        top_y = new_y;
        if (point_solid(p->x + 2, top_y) || point_solid(p->x + PLAYER_W - 3, top_y)) {
            ty = top_y / TILE_SIZE;
            new_y = (ty + 1) * TILE_SIZE;
            p->vy = 0;
            /* Hit block from below - check for question block */
            if (level_get_tile(p->x / TILE_SIZE + 1, ty) == TILE_QBLOCK ||
                level_get_tile((p->x + PLAYER_W - 3) / TILE_SIZE, ty) == TILE_QBLOCK) {
                int btx = (p->x + PLAYER_W / 2) / TILE_SIZE;
                if (level_get_tile(btx, ty) == TILE_QBLOCK) {
                    level_set_tile(btx, ty, TILE_QBLOCK_HIT);
                    sound_collect();
                    p->score += 100;
                }
            }
        }
    } else {
        /* Moving down - check feet */
        bot_y = new_y + ph;
        p->on_ground = 0;

        if (point_solid(p->x + 2, bot_y) || point_solid(p->x + PLAYER_W - 3, bot_y)) {
            ty = bot_y / TILE_SIZE;
            new_y = ty * TILE_SIZE - ph;
            p->vy = 0;
            p->on_ground = 1;
        }
        /* Check platforms (only when falling) */
        else if (p->vy > 0) {
            int foot_ty = bot_y / TILE_SIZE;
            int prev_bot = p->y + ph;
            int prev_ty = prev_bot / TILE_SIZE;
            if (foot_ty != prev_ty &&
                (on_platform(p->x + 2, bot_y) ||
                 on_platform(p->x + PLAYER_W - 3, bot_y))) {
                new_y = foot_ty * TILE_SIZE - ph;
                p->vy = 0;
                p->on_ground = 1;
            }
        }
    }
    p->y = new_y;

    /* Fall off bottom = death */
    if (p->y > TILES_Y * TILE_SIZE) {
        p->health = 0;
    }

    /* Animation state */
    if (!p->on_ground) {
        p->state = (p->vy < 0) ? 2 : 3; /* jump or fall */
    } else if (p->vx != 0) {
        p->state = 1; /* walk */
        p->frame_timer++;
        if (p->frame_timer >= 6) {
            p->frame_timer = 0;
            p->walk_frame = (p->walk_frame + 1) % 4;
        }
    } else {
        p->state = 0; /* idle */
        p->walk_frame = 0;
    }

    /* Check enemy collision */
    if (p->invuln == 0) {
        if (p->vy > 0 && enemies_check_stomp(p->x, p->y, PLAYER_W, ph)) {
            /* Stomped an enemy - bounce up */
            p->vy = JUMP_FORCE / 2;
            p->score += 200;
            sound_stomp();
        } else if (enemies_check_touch(p->x, p->y, PLAYER_W, ph)) {
            p->health--;
            p->invuln = 60; /* 1.2 seconds invulnerability */
            sound_hurt();
        }
    }

    /* Check item collection */
    {
        int item_type = items_check_collect(p->x, p->y, PLAYER_W, ph);
        if (item_type > 0) {
            switch (item_type) {
            case ENT_BOARD:    p->score += 50;  break;
            case ENT_DISKETTE: p->score += 100; break;
            case ENT_BALL:     p->score += 75;  break;
            case ENT_PILLOW_C: p->score += 150; break;
            case ENT_PLANT:    p->score += 200; break;
            case ENT_BRIEFCASE:
                p->invuln = 150; /* 3 seconds */
                sound_powerup();
                break;
            case ENT_HEART:
                if (p->health < p->max_health) p->health++;
                sound_powerup();
                break;
            case ENT_PILLOW_P:
                p->lives++;
                sound_powerup();
                break;
            case ENT_BEER:
            case ENT_WINE:
                p->speed_boost = 200; /* 4 seconds */
                sound_powerup();
                break;
            }
            if (item_type >= ENT_BOARD && item_type <= ENT_PLANT)
                sound_collect();
        }
    }
}

/*
 * Draw player character
 * RJ: tall, slender. Dale: shorter, rounder.
 */
void player_draw(struct RastPort *rp, Player *p, int cam_x) {
    int sx = p->x - cam_x;
    int sy = p->y;
    int bw = (p->character == CHAR_RJ) ? 10 : 12; /* body width */
    int ox = (PLAYER_W - bw) / 2; /* center offset */
    int leg_off = 0;

    /* Off screen check */
    if (sx + PLAYER_W < 0 || sx >= SCREEN_W) return;

    /* Invulnerability flash */
    if (p->invuln > 0 && (p->invuln & 4)) return;

    /* Walking leg offset for animation */
    if (p->state == 1) {
        static const int leg_anim[] = { 0, 2, 0, -2 };
        leg_off = leg_anim[p->walk_frame & 3];
    }

    if (p->character == CHAR_RJ) {
        /* RJ - tall, slender */
        /* Hair (grey) */
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx + ox + 1, sy, sx + ox + bw - 2, sy + 2);

        /* Face (skin) */
        SetAPen(rp, COL_SKIN);
        RectFill(rp, sx + ox + 1, sy + 3, sx + ox + bw - 2, sy + 7);

        /* Eyes */
        SetAPen(rp, COL_BLACK);
        if (p->dir == DIR_RIGHT) {
            RectFill(rp, sx + ox + 5, sy + 4, sx + ox + 6, sy + 5);
        } else {
            RectFill(rp, sx + ox + 3, sy + 4, sx + ox + 4, sy + 5);
        }

        /* Beard (dark grey) */
        SetAPen(rp, COL_BLACK);
        RectFill(rp, sx + ox + 2, sy + 7, sx + ox + bw - 3, sy + 8);

        /* T-shirt (white) */
        SetAPen(rp, COL_WHITE);
        RectFill(rp, sx + ox, sy + 9, sx + ox + bw - 1, sy + 17);

        /* Jeans (blue) */
        SetAPen(rp, COL_BLUE);
        RectFill(rp, sx + ox, sy + 18, sx + ox + bw / 2 - 1 + leg_off, sy + 24);
        RectFill(rp, sx + ox + bw / 2 + 1 - leg_off, sy + 18,
                 sx + ox + bw - 1, sy + 24);

        /* Shoes (brown) */
        SetAPen(rp, COL_BROWN);
        RectFill(rp, sx + ox, sy + 25, sx + ox + bw / 2 - 1 + leg_off, sy + 27);
        RectFill(rp, sx + ox + bw / 2 + 1 - leg_off, sy + 25,
                 sx + ox + bw - 1, sy + 27);
    } else {
        /* Dale - shorter, rounder */
        /* Hair (grey) */
        SetAPen(rp, COL_GREY);
        RectFill(rp, sx + ox, sy, sx + ox + bw - 1, sy + 2);

        /* Face (skin) */
        SetAPen(rp, COL_SKIN);
        RectFill(rp, sx + ox, sy + 3, sx + ox + bw - 1, sy + 7);

        /* Eyes */
        SetAPen(rp, COL_BLACK);
        if (p->dir == DIR_RIGHT) {
            RectFill(rp, sx + ox + 6, sy + 4, sx + ox + 7, sy + 5);
        } else {
            RectFill(rp, sx + ox + 4, sy + 4, sx + ox + 5, sy + 5);
        }

        /* Beard */
        SetAPen(rp, COL_BLACK);
        RectFill(rp, sx + ox + 1, sy + 7, sx + ox + bw - 2, sy + 8);

        /* T-shirt (white, wider) */
        SetAPen(rp, COL_WHITE);
        RectFill(rp, sx + ox - 1, sy + 9, sx + ox + bw, sy + 15);

        /* Jeans (blue, wider) */
        SetAPen(rp, COL_BLUE);
        RectFill(rp, sx + ox, sy + 16, sx + ox + bw / 2 - 1 + leg_off, sy + 20);
        RectFill(rp, sx + ox + bw / 2 + 1 - leg_off, sy + 16,
                 sx + ox + bw - 1, sy + 20);

        /* Shoes */
        SetAPen(rp, COL_BROWN);
        RectFill(rp, sx + ox, sy + 21, sx + ox + bw / 2 - 1 + leg_off, sy + 23);
        RectFill(rp, sx + ox + bw / 2 + 1 - leg_off, sy + 21,
                 sx + ox + bw - 1, sy + 23);
    }
}
