#include <cstdarg>
#include <cstdio>

#include "mods/hook.hpp"
#include "mods/service.hpp"
#include "mods/svc/hook.h"
#include "mods/svc/log.h"

#include "JSystem/J2DGraph/J2DPicture.h"
#include "JSystem/J2DGraph/J2DWindow.h"
#include "JSystem/JUtility/JUTTexture.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "d/d_meter2_draw.h"
#include "d/d_meter2_info.h"
#include "d/d_menu_window.h"
#include "d/d_menu_item_explain.h"

#define private public
#include "d/d_menu_ring.h"
#undef private

DEFINE_MOD();

IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);

// Swap B (attack) <-> X (item) in the per-frame gameplay button snapshot.
DEFINE_HOOK(&daAlink_c::setStickData, SetStickData);

static u8 swap_b_x(u8 flags) {
    u8 s = flags & ~(daAlink_c::BTN_B | daAlink_c::BTN_X);
    if (flags & daAlink_c::BTN_B) s |= daAlink_c::BTN_X;
    if (flags & daAlink_c::BTN_X) s |= daAlink_c::BTN_B;
    return s;
}

static void on_set_stick_data_post(ModContext*, void* args, void*, void*) {
    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    link->mItemTrigger = swap_b_x(link->mItemTrigger);
    link->mItemButton  = swap_b_x(link->mItemButton);
}

// Swap B/X letter textures and turn the attack-button indicator blue.
// Swap R -> B on any red-dominant vertex.
DEFINE_HOOK(&dMeter2Draw_c::initButton, InitButton);

static bool is_red(JUtility::TColor c) { return c.r > c.b && c.r > c.g; }
static JUtility::TColor swap_rb(JUtility::TColor c) {
    u8 t = c.r; c.r = c.b; c.b = t; return c;
}
static bool fix_quad(JUtility::TColor& a, JUtility::TColor& b,
                     JUtility::TColor& c, JUtility::TColor& d) {
    if (!is_red(a) && !is_red(b) && !is_red(c) && !is_red(d)) return false;
    a = swap_rb(a); b = swap_rb(b); c = swap_rb(c); d = swap_rb(d);
    return true;
}

static const ResTIMG* pane_texture(J2DPane* pane) {
    u32 k = pane->getKind();
    if (k == MULTI_CHAR('PIC1') || k == MULTI_CHAR('PIC2')) {
        JUTTexture* t = static_cast<J2DPicture*>(pane)->getTexture(0);
        return t ? t->getTexInfo() : nullptr;
    }
    if (k == MULTI_CHAR('WIN1') || k == MULTI_CHAR('WIN2')) {
        J2DWindow* w = static_cast<J2DWindow*>(pane);
        JUTTexture* t = nullptr;
        J2DMaterial* m = w->getFrameMaterial(0);
        if (m && m->getTevBlock()) t = m->getTevBlock()->getTexture(0);
        if (!t) t = w->getFrameTexture(0, 0);
        return t ? t->getTexInfo() : nullptr;
    }
    return nullptr;
}

static void walk_and_fix(J2DPane* pane,
                         const ResTIMG* bText, const ResTIMG* xText,
                         const ResTIMG* maru,
                         int& letterSwaps, int& recolorTouches) {
    if (pane == nullptr) return;

    const ResTIMG* tex = pane_texture(pane);
    u32 kind = pane->getKind();
    bool isPic = kind == MULTI_CHAR('PIC1') || kind == MULTI_CHAR('PIC2');
    bool isWin = kind == MULTI_CHAR('WIN1') || kind == MULTI_CHAR('WIN2');

    if (tex && (tex == bText || tex == xText) && isPic) {
        static_cast<J2DPicture*>(pane)->changeTexture(
            tex == bText ? xText : bText, 0);
        letterSwaps++;
    }

    if (tex && tex == maru) {
        if (isPic) {
            J2DPicture* p = static_cast<J2DPicture*>(pane);
            auto bw = p->getBlack(), wh = p->getWhite();
            if (is_red(wh) || is_red(bw)) {
                p->setBlackWhite(swap_rb(bw), swap_rb(wh));
                recolorTouches++;
            }
            JUtility::TColor c[4];
            for (int i = 0; i < 4; i++) c[i] = p->corner(i);
            if (fix_quad(c[0], c[1], c[2], c[3])) {
                p->setCornerColor(c[0], c[1], c[2], c[3]);
                recolorTouches++;
            }
        } else if (isWin) {
            J2DWindow* w = static_cast<J2DWindow*>(pane);
            auto bw = w->getBlack(), wh = w->getWhite();
            if (is_red(wh) || is_red(bw)) {
                w->setBlackWhite(swap_rb(bw), swap_rb(wh));
                recolorTouches++;
            }
            J2DWindow::TContentsColor cc;
            w->getContentsColor(cc);
            if (fix_quad(cc.field_0x0, cc.field_0x4, cc.field_0x8, cc.field_0xc)) {
                w->setContentsColor(cc.field_0x0, cc.field_0x4,
                                   cc.field_0x8, cc.field_0xc);
                recolorTouches++;
            }
        }
    }

    for (J2DPane* child = pane->getFirstChildPane(); child;
         child = child->getNextChildPane()) {
        walk_and_fix(child, bText, xText, maru, letterSwaps, recolorTouches);
    }
}

static void on_init_button_post(ModContext*, void* args, void*, void*) {
    dMeter2Draw_c* meter = mods::arg<dMeter2Draw_c*>(args, 0);
    J2DScreen* screen = meter->getMainScreenPtr();
    JKRArchive* arc = dComIfGp_getMain2DArchive();
    if (screen == nullptr || arc == nullptr) {
        return svc_log->warn(mod_ctx, "HUD fixup skipped: screen/archive unavailable");
    }

    auto* bText = static_cast<const ResTIMG*>(
        arc->getResource('TIMG', "tt_zelda_button_b_text.bti"));
    auto* xText = static_cast<const ResTIMG*>(
        arc->getResource('TIMG', "tt_zelda_button_x_text.bti"));
    auto* maru  = static_cast<const ResTIMG*>(
        arc->getResource('TIMG', "tt_zelda_button_ab_maru.bti"));

    if (bText == nullptr || xText == nullptr) {
        return svc_log->warn(mod_ctx, "letter textures not found, skipping HUD fixup");
    }

    int swaps = 0, touches = 0;
    walk_and_fix(screen, bText, xText, maru, swaps, touches);
}

// Ring menu: after the input swap, physical B assigns to X-slot, physical X closes.
// Y assigns to Y-slot (unchanged).
DEFINE_HOOK(&dMenu_Ring_c::isMoveEnd,         IsMoveEnd);
DEFINE_HOOK(&dMenu_Ring_c::setActiveCursor,   SetActiveCursor);

static void on_is_move_end_replace(ModContext*, void* args, void* ret, void*) {
    dMenu_Ring_c* self = mods::arg<dMenu_Ring_c*>(args, 0);
    bool* result = static_cast<bool*>(ret);
    *result = false;

    if (self->mStatus != dMenu_Ring_c::STATUS_WAIT) return;
    if (self->mOldStatus == dMenu_Ring_c::STATUS_EXPLAIN_FORCE) return;
    if (self->mOldStatus == dMenu_Ring_c::STATUS_EXPLAIN) return;

    if (dMw_UP_TRIGGER() || dMw_DOWN_TRIGGER() ||
        mDoCPd_c::getTrigX(PAD_1) ||
        dMeter2Info_getWarpStatus() == 2 ||
        dMeter2Info_getWarpStatus() == 1 ||
        dMeter2Info_isTouchKeyCheck(0xe))
    {
        if (dMw_UP_TRIGGER())
            self->mRingOrigin = 0;
        else if (dMw_DOWN_TRIGGER())
            self->mRingOrigin = 2;
        else
            self->mRingOrigin = 0xFF;

        Z2GetAudioMgr()->seStart(Z2SE_ITEM_RING_OUT, nullptr, 0, 0,
                                 1.0f, 1.0f, -1.0f, -1.0f, 0);
        dMeter2Info_set2DVibrationM();
        *result = true;
    }
}

static void hard_assign_slot(dMenu_Ring_c* self, u8 slot) {
    for (int i = 0; i < MAX_SELECT_ITEM; i++)
        self->setSelectItemForce(i);
    self->field_0x6b3 = slot;
    if (self->checkCombineBomb(self->field_0x6b3)) return;
    self->setItem();
    if (self->mpItemExplain->getStatus() == 0) {
        self->setStatus(dMenu_Ring_c::STATUS_WAIT);
        self->stick_wait_init();
    }
}

static void on_set_active_cursor_replace(ModContext*, void* args, void*, void*) {
    dMenu_Ring_c* self = mods::arg<dMenu_Ring_c*>(args, 0);
    const u8 item = dComIfGs_getItem(self->mItemSlots[self->mCurrentSlot], false);

    if (self->mStatus != dMenu_Ring_c::STATUS_WAIT) return;
    if (self->mOldStatus == dMenu_Ring_c::STATUS_EXPLAIN_FORCE) return;
    if (self->mOldStatus == dMenu_Ring_c::STATUS_EXPLAIN) return;
    if (self->mpItemExplain->getStatus() != 0) return;

    if (mDoCPd_c::getTrigR(PAD_1) && !self->mPlayerIsWolf && item != dItemNo_NONE_e) {
        for (int i = 0; i < MAX_SELECT_ITEM; i++)
            self->setSelectItemForce(i);
        self->setMixItem();
        return;
    }

    if (mDoCPd_c::getTrigB(PAD_1) && !self->mPlayerIsWolf && item != dItemNo_NONE_e) {
        hard_assign_slot(self, 0);
        return;
    }

    if (mDoCPd_c::getTrigY(PAD_1) && !self->mPlayerIsWolf && item != dItemNo_NONE_e) {
        hard_assign_slot(self, 1);
        return;
    }

    if (mDoCPd_c::getTrigB(PAD_1) || mDoCPd_c::getTrigY(PAD_1)) {
        Z2GetAudioMgr()->seStart(Z2SE_SYS_ERROR, nullptr, 0, 0,
                                 1.0f, 1.0f, -1.0f, -1.0f, 0);
    }
}

extern "C" {

MOD_EXPORT ModResult mod_initialize(ModError* error) {
    ModResult r = mods::hook_add_post<SetStickData>(svc_hook, on_set_stick_data_post);
    if (r != MOD_OK) { svc_log->error(mod_ctx, "failed to hook setStickData"); return r; }

    r = mods::hook_add_post<InitButton>(svc_hook, on_init_button_post);
    if (r != MOD_OK) { svc_log->error(mod_ctx, "failed to hook initButton"); return r; }

    r = mods::hook_replace<IsMoveEnd>(svc_hook, on_is_move_end_replace);
    if (r != MOD_OK) { svc_log->error(mod_ctx, "failed to hook isMoveEnd"); return r; }

    r = mods::hook_replace<SetActiveCursor>(svc_hook, on_set_active_cursor_replace);
    if (r != MOD_OK) { svc_log->error(mod_ctx, "failed to hook setActiveCursor"); return r; }

    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    return MOD_OK;
}

}
