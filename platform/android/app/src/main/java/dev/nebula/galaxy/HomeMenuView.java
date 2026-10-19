// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * The HOME Menu over the paused game (the game opens it with the HOME button): back to the game, reset, or quit to
 * Nebula's launcher, by touch or with a controller.
 */
final class HomeMenuView extends FrameLayout {
    static final int RESUME = 0;
    static final int QUIT = 1;
    static final int RESET = 2;

    interface Listener {
        void onHomeChoice(int choice);

        void onTouchControlsToggled(boolean shown);
    }

    private final Listener listener;
    private final float dp;
    private final LinearLayout card;
    private final TextView resume;
    private final TextView controls;
    private boolean controlsShown = true;

    HomeMenuView(Context context, Listener listener) {
        super(context);
        this.listener = listener;
        dp = context.getResources().getDisplayMetrics().density;
        setBackgroundColor(0xB8000000);
        setClickable(true);  // the dim takes the touches meant for the game
        setVisibility(GONE);

        card = new LinearLayout(context);
        card.setOrientation(LinearLayout.VERTICAL);
        final int pad = px(24);
        card.setPadding(pad, pad, pad, px(18));
        final GradientDrawable surface = new GradientDrawable();
        surface.setColor(0xF2141629);
        surface.setCornerRadius(px(28));
        surface.setStroke(px(1), 0x29FFFFFF);
        card.setBackground(surface);
        card.setElevation(px(12));

        final TextView title = new TextView(context);
        title.setText("HOME Menu");
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
        title.setTypeface(Typeface.create(Typeface.DEFAULT, 700, false));
        card.addView(title);
        final TextView subtitle = new TextView(context);
        subtitle.setText("The game is paused.");
        subtitle.setTextColor(0xB3FFFFFF);
        subtitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        subtitle.setPadding(0, px(4), 0, px(18));
        card.addView(subtitle);

        resume = action("Back to the game", 0xFFC9BEFF, 0xFF1A1533);
        resume.setOnClickListener(v -> choose(RESUME));
        card.addView(resume);
        final TextView reset = action("Reset the game", 0x1FFFFFFF, Color.WHITE);
        reset.setOnClickListener(v -> choose(RESET));
        card.addView(reset);
        final TextView quit = action("Quit to Nebula", 0x1FFFFFFF, 0xFFFFB4AB);
        quit.setOnClickListener(v -> choose(QUIT));
        card.addView(quit);
        controls = action("", 0x00000000, 0xCCFFFFFF);
        controls.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        controls.setOnClickListener(v -> {
            controlsShown = !controlsShown;
            updateControlsLabel();
            listener.onTouchControlsToggled(controlsShown);
        });
        card.addView(controls);
        updateControlsLabel();

        // Centred, and scrolling where the screen is shorter than the card (a phone held sideways).
        final FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(px(400), ViewGroup.LayoutParams.WRAP_CONTENT);
        params.gravity = Gravity.CENTER;
        final FrameLayout centre = new FrameLayout(context);
        centre.setPadding(0, px(16), 0, px(16));
        centre.addView(card, params);
        final ScrollView scroller = new ScrollView(context);
        scroller.setFillViewport(true);
        scroller.setVerticalScrollBarEnabled(false);
        scroller.addView(centre, new ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT));
        addView(scroller, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
    }

    private int px(float value) {
        return Math.round(value * dp);
    }

    private TextView action(String label, int background, int ink) {
        final TextView view = new TextView(getContext());
        view.setText(label);
        view.setTextColor(ink);
        view.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        view.setTypeface(Typeface.create(Typeface.DEFAULT, 600, false));
        view.setGravity(Gravity.CENTER);
        view.setMinHeight(px(52));
        view.setFocusable(true);
        final GradientDrawable shape = new GradientDrawable();
        shape.setColor(background);
        shape.setCornerRadius(px(26));
        final GradientDrawable mask = new GradientDrawable();
        mask.setColor(Color.WHITE);
        mask.setCornerRadius(px(26));
        view.setBackground(new RippleDrawable(ColorStateList.valueOf(0x40FFFFFF), shape, mask));
        // The controller's focus: a ring round the focused action.
        view.setOnFocusChangeListener((v, focused) -> {
            shape.setStroke(focused ? px(2) : 0, 0xFFFFFFFF);
        });
        final LinearLayout.LayoutParams params =
                new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        params.topMargin = px(10);
        view.setLayoutParams(params);
        return view;
    }

    private void updateControlsLabel() {
        controls.setText(controlsShown ? "Hide the touch controls" : "Show the touch controls");
    }

    void setControlsShown(boolean shown) {
        controlsShown = shown;
        updateControlsLabel();
    }

    boolean showing() {
        return getVisibility() == VISIBLE;
    }

    void show() {
        if (showing()) {
            return;
        }
        final int width = getWidth() > 0 ? getWidth() : getResources().getDisplayMetrics().widthPixels;
        card.getLayoutParams().width = Math.min(px(400), Math.round(width * 0.9f));
        card.requestLayout();
        setVisibility(VISIBLE);
        setAlpha(0.0f);
        card.setScaleX(0.94f);
        card.setScaleY(0.94f);
        animate().alpha(1.0f).setDuration(160).start();
        card.animate().scaleX(1.0f).scaleY(1.0f).setDuration(200).start();
        resume.requestFocus();
    }

    void hide() {
        if (!showing()) {
            return;
        }
        animate().alpha(0.0f).setDuration(140).withEndAction(() -> setVisibility(GONE)).start();
    }

    private void choose(int choice) {
        hide();
        listener.onHomeChoice(choice);
    }

    /** A controller's key while the menu shows: focus moves with the D-pad, A chooses, B goes back to the game. */
    boolean handleKey(KeyEvent event) {
        final int code = event.getKeyCode();
        if (event.getAction() != KeyEvent.ACTION_DOWN) {
            return true;
        }
        final View focused = findFocus();
        switch (code) {
        case KeyEvent.KEYCODE_BUTTON_A:
        case KeyEvent.KEYCODE_DPAD_CENTER:
        case KeyEvent.KEYCODE_ENTER:
            if (focused != null) {
                focused.performClick();
            }
            return true;
        case KeyEvent.KEYCODE_BUTTON_B:
        case KeyEvent.KEYCODE_BACK:
        case KeyEvent.KEYCODE_BUTTON_MODE:
        case KeyEvent.KEYCODE_ESCAPE:
            choose(RESUME);
            return true;
        case KeyEvent.KEYCODE_DPAD_UP:
        case KeyEvent.KEYCODE_DPAD_DOWN: {
            final View next = focused != null
                    ? focused.focusSearch(code == KeyEvent.KEYCODE_DPAD_UP ? View.FOCUS_UP : View.FOCUS_DOWN)
                    : resume;
            if (next != null && isAncestorOf(next)) {
                next.requestFocus();
            }
            return true;
        }
        default:
            return true;
        }
    }

    private boolean isAncestorOf(View view) {
        for (Object parent = view.getParent(); parent != null; parent = ((View) parent).getParent()) {
            if (parent == this) {
                return true;
            }
            if (!(parent instanceof View)) {
                return false;
            }
        }
        return false;
    }
}
