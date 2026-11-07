// SPDX-License-Identifier: GPL-3.0-only
package dev.nebula.galaxy;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Insets;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.SystemClock;
import android.view.HapticFeedbackConstants;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/**
 * The on-screen Wii Remote and Nunchuk over the picture. Every touch comes here: a finger that lands on a control
 * presses it (and may slide on to the next), the first finger on the left side of the picture is a floating stick
 * (on a pointer screen, only on the stick's circle), and any other finger is the pointer.
 */
final class TouchControlsView extends View {
    // As platform/sdl/sdl_host.h's TouchButton.
    static final int A = 1, B = 1 << 1, Z = 1 << 2, C = 1 << 3, SHAKE = 1 << 4, MINUS = 1 << 5, PLUS = 1 << 6,
            HOME = 1 << 7, ONE = 1 << 8, TWO = 1 << 9, UP = 1 << 10, DOWN = 1 << 11, LEFT = 1 << 12, RIGHT = 1 << 13;

    interface Listener {
        /** The held buttons and the stick (-1..1, up positive) changed. */
        void onControls(int buttons, float stickX, float stickY);

        /** A finger on the picture: 0 down, 1 moved, 2 up; x, y in pixels. */
        void onPointer(int action, int id, float x, float y);

        /** The game is on a pointer screen: only the stick's own circle is the stick. */
        boolean menuContext();

        boolean titleScreen();

        /** A finger at x, y (pixels) lands on something the pointer grabs: it points, whatever side of the screen it is on. */
        boolean touchTarget(float x, float y);

        /** The player pressed OK. */
        void arrangingDone();

        /** What General's rows show for the screen as it is now; 0: the size the game gives this screen. */
        void touchSettings(int sizePercent, int opacityPercent, boolean haptics);
    }

    // What each finger (by pointer id, 0..31) landed on: a button's index or one of these. Arrays, so a touch
    // allocates nothing.
    private static final int MAX_POINTERS = 32;
    private static final int TARGET_NONE = -100;
    private static final int TARGET_STICK = -1;
    private static final int TARGET_POINTER = -2;
    private static final int TARGET_DPAD = -3;
    private static final float STICK_DEAD_ZONE = 0.06f;
    // Places in `buttons`.
    private static final int HOME_BUTTON = 6;
    private static final int TWO_BUTTON = 9;
    // Places in a saved layout: the buttons', then these.
    private static final int ITEM_STICK = 10;
    private static final int ITEM_DPAD = 11;
    private static final float SIZE_MIN = 0.4f;
    private static final float SIZE_MAX = 2.0f;

    private static final class Button {
        final int bit;
        final String label;
        final boolean spinIcon;
        final boolean homeIcon;
        float x, y, radius;

        Button(int bit, String label, boolean spinIcon, boolean homeIcon) {
            this.bit = bit;
            this.label = label;
            this.spinIcon = spinIcon;
            this.homeIcon = homeIcon;
        }
    }

    private final Listener listener;
    private final float dp;
    private final List<Button> buttons = new ArrayList<>();
    private final int[] targets = new int[MAX_POINTERS];
    private final int[] pointerBits = new int[MAX_POINTERS];  // the buttons each finger holds
    private final boolean[] zLeft = new boolean[MAX_POINTERS];
    private final boolean[] zQuick = new boolean[MAX_POINTERS];
    private final long[] zDownMs = new long[MAX_POINTERS];
    private static final long Z_QUICK_SLIDE_MS = 180;
    private static final float Z_QUICK_SLIDE_STICK = 0.3f;
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint text = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint line = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path path = new Path();
    private final RectF rect = new RectF();

    private float opacity;
    private float scale;
    private boolean haptics;
    private boolean shown = true;
    private Insets safe = Insets.NONE;

    // The D-pad: centre and reach.
    private float dpadX, dpadY, dpadRadius;
    // The stick: where it rests, its reach, and while held, where it was touched and where the finger is.
    private float stickRestX, stickRestY, stickRadius;
    private float stickBaseX, stickBaseY, stickKnobX, stickKnobY;
    private int stickPointer = -1;
    private boolean pointerScreen = false;
    private float homeX;
    private float homeY;
    private float stickZoneRight, stickZoneTop;

    private int heldButtons;
    private float stickX, stickY;

    // The arrangement of this shape of screen.
    private final TouchLayouts layouts;
    private TouchLayouts.Layout layout = new TouchLayouts.Layout();
    private String layoutKey;
    private boolean stickOnLeft = true;
    private boolean layoutLoaded;

    // Arranging: the control under the one finger that drags. Save, Reset and Cancel are the game's own window.
    private boolean arranging;
    private int selected = -1;
    private int dragPointer = -1;
    private float dragOffsetX, dragOffsetY;
    private final float[] arrangedX = new float[TouchLayouts.ITEMS];  // the places when arranging began, for Cancel
    private final float[] arrangedY = new float[TouchLayouts.ITEMS];

    TouchControlsView(Context context, Listener listener) {
        super(context);
        this.listener = listener;
        layouts = new TouchLayouts(context);
        dp = context.getResources().getDisplayMetrics().density;
        buttons.add(new Button(A, "A", false, false));
        buttons.add(new Button(B, "B", false, false));
        buttons.add(new Button(SHAKE, "", true, false));
        buttons.add(new Button(Z, "Z", false, false));
        buttons.add(new Button(C, "C", false, false));
        buttons.add(new Button(MINUS, "−", false, false));
        buttons.add(new Button(HOME, "", false, true));
        buttons.add(new Button(PLUS, "+", false, false));
        buttons.add(new Button(ONE, "1", false, false));
        buttons.add(new Button(TWO, "2", false, false));
        Arrays.fill(targets, TARGET_NONE);
        stroke.setStyle(Paint.Style.STROKE);
        line.setStyle(Paint.Style.STROKE);
        line.setStrokeCap(Paint.Cap.ROUND);
        line.setStrokeJoin(Paint.Join.ROUND);
        text.setTextAlign(Paint.Align.CENTER);
        text.setTypeface(Typeface.create(Typeface.DEFAULT, 700, false));
        setFocusable(false);
        configure(0.6f, true);
    }

    void configure(float opacity, boolean haptics) {
        this.opacity = Math.max(0.2f, Math.min(1.0f, opacity));
        this.haptics = haptics;
        layoutControls(getWidth(), getHeight());
        invalidate();
    }

    /** Shown or hidden (hidden: every finger is the pointer). Hiding releases whatever was held. */
    /** The game went to or from a pointer screen (GameActivity.menuContextChanged). */
    void syncMenuContext() {
        final boolean menu = listener.menuContext();
        if (menu == pointerScreen) {
            return;
        }
        pointerScreen = menu;
        placeHome();
        for (int i = 0; i < MAX_POINTERS; ++i) {
            if (targets[i] != TARGET_POINTER) {
                targets[i] = TARGET_NONE;
            }
        }
        Arrays.fill(pointerBits, 0);
        stickPointer = -1;
        publish(0, 0.0f, 0.0f);
        invalidate();
    }

    // HOME over the picture on a pointer screen: in the top corner, clear of the game's own buttons. Arranging (begun
    // from the pause screen) shows it in its place in play, the place being arranged.
    private void placeHome() {
        final Button home = buttons.get(HOME_BUTTON);
        final boolean corner = pointerScreen && !arranging;
        home.x = corner ? buttons.get(TWO_BUTTON).x : homeX;
        home.y = corner ? buttons.get(TWO_BUTTON).y : homeY;
        if (corner && getWidth() > 0 && getHeight() > 0) {
            // The file card (FileInfo: 286 canvas units either side, its foot 110 under the canvas's top) spans a screen
            // narrower than 16:9 at the top: HOME goes under its foot.
            final float aspect = (float) getWidth() / getHeight();
            final float unit = aspect < 4.0f / 3.0f ? getWidth() / 608.0f : getHeight() / 456.0f;
            if (home.x - home.radius < getWidth() / 2.0f + 286.0f * unit) {
                home.y = Math.max(home.y, 118.0f * unit + home.radius);
            }
        }
    }

    void setControlsShown(boolean value) {
        if (shown == value) {
            return;
        }
        shown = value;
        if (!value) {
            for (int i = 0; i < MAX_POINTERS; ++i) {
                if (targets[i] != TARGET_POINTER) {
                    targets[i] = TARGET_NONE;
                }
            }
            Arrays.fill(pointerBits, 0);
            stickPointer = -1;
            publish(0, 0.0f, 0.0f);
        }
        invalidate();
    }

    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        safe = insets.getInsetsIgnoringVisibility(WindowInsets.Type.displayCutout() | WindowInsets.Type.systemGestures());
        layoutControls(getWidth(), getHeight());
        return insets;
    }

    @Override
    protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        layoutControls(width, height);
    }

    // The size a screen starts at: controls in proportion to it, so a small phone isn't covered by them.
    private float startingSize(int width, int height) {
        final float shortest = Math.min(width, height) / dp;
        return Math.max(0.55f, Math.min(1.1f, shortest / 400.0f));
    }

    private void layoutControls(int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }
        final String key = TouchLayouts.key(width, height, dp);
        if (!key.equals(layoutKey)) {
            if (arranging && layoutKey != null) {
                layouts.save(layoutKey, layout);
            }
            layoutKey = key;
            layout = layouts.load(key);
            selected = -1;
            layoutLoaded = true;
        } else {
            layoutLoaded = false;
        }
        scale = Float.isNaN(layout.size) ? startingSize(width, height) : layout.size;
        final float u = dp * scale;
        final float left = Math.max(safe.left, 0) + 20 * dp;
        final float right = width - Math.max(safe.right, 0) - 20 * dp;
        final float top = Math.max(safe.top, 0) + 14 * dp;
        final float bottom = height - Math.max(safe.bottom, 0) - 18 * dp;

        // The right thumb's arc: A at its centre, Spin, B and Z around it.
        final float ra = 40 * u;
        final float rs = 29 * u;
        final Button a = buttons.get(0);
        a.radius = ra;
        a.x = right - ra;
        a.y = bottom - ra;
        final float reach = ra + rs + 14 * u;
        placeOnArc(buttons.get(2), a, reach, 98, rs);   // Spin
        placeOnArc(buttons.get(1), a, reach, 142, rs);  // B
        placeOnArc(buttons.get(3), a, reach, 186, rs);  // Z
        // C above the arc, at the edge.
        final Button c = buttons.get(4);
        c.radius = 24 * u;
        c.x = right - c.radius;
        c.y = a.y - reach - rs - 34 * u;

        // The small buttons: - HOME + at the top centre, 1 2 at the top right (a row lower where the screen is too
        // narrow for both).
        final float rm = 19 * u;
        final float cx = width / 2.0f;
        final float row = top + rm;
        place(buttons.get(5), cx - 3.0f * rm, row, rm);
        place(buttons.get(6), cx, row, rm * 1.1f);
        place(buttons.get(7), cx + 3.0f * rm, row, rm);
        final float row12 = cx + 4.2f * rm > right - 4.4f * rm ? row + 2.8f * rm : row;
        place(buttons.get(9), right - rm, row12, rm);
        place(buttons.get(8), right - 3.4f * rm, row12, rm);

        // The stick at the bottom left; the D-pad at the top left, or on a tall screen (a phone upright, the Fold
        // open) just above the stick, where the thumb reaches it.
        dpadRadius = 46 * u;
        stickRadius = 62 * u;
        stickRestX = left + stickRadius + 10 * u;
        stickRestY = bottom - stickRadius;
        dpadX = left + dpadRadius;
        dpadY = height > width ? stickRestY - stickRadius - dpadRadius - 28 * u : top + dpadRadius + 8 * u;
        stickZoneRight = width * 0.46f;
        stickZoneTop = dpadY + dpadRadius + 12 * u;
        text.setTextSize(20 * u);

        // Where the player put them for this shape of screen, on the screen however large they now are.
        for (int i = 0; i < buttons.size(); ++i) {
            final Button button = buttons.get(i);
            if (layout.moved(i)) {
                button.x = onScreen(layout.x[i] * width, button.radius, width);
                button.y = onScreen(layout.y[i] * height, button.radius, height);
            }
        }
        if (layout.moved(ITEM_DPAD)) {
            dpadX = onScreen(layout.x[ITEM_DPAD] * width, dpadRadius, width);
            dpadY = onScreen(layout.y[ITEM_DPAD] * height, dpadRadius, height);
        }
        if (layout.moved(ITEM_STICK)) {
            stickRestX = onScreen(layout.x[ITEM_STICK] * width, stickRadius, width);
            stickRestY = onScreen(layout.y[ITEM_STICK] * height, stickRadius, height);
        }
        stickOnLeft = stickRestX < width / 2.0f;
        if (layout.moved(ITEM_STICK) || layout.moved(ITEM_DPAD)) {
            // The stick floats in the lower part of its side, above which the D-pad may sit.
            final boolean dpadAbove = (dpadX < width / 2.0f) == stickOnLeft && dpadY < stickRestY;
            stickZoneTop = dpadAbove ? dpadY + dpadRadius + 12 * u : Math.max(0.0f, stickRestY - stickRadius * 2.6f);
        }
        homeX = buttons.get(HOME_BUTTON).x;
        homeY = buttons.get(HOME_BUTTON).y;
        placeHome();
        if (layoutLoaded) {
            reportSettings();
        }
    }

    private static float onScreen(float at, float radius, int size) {
        return Math.max(radius, Math.min(size - radius, at));
    }

    private void reportSettings() {
        listener.touchSettings(Float.isNaN(layout.size) ? 0 : Math.round(layout.size * 100.0f), Math.round(opacity * 100.0f), haptics);
    }

    /** From General's rows. */
    void applySettings(int sizePercent, int opacityPercent, boolean vibrate) {
        layout.size = sizePercent == 0 ? Float.NaN : Math.max(SIZE_MIN, Math.min(SIZE_MAX, sizePercent / 100.0f));
        layouts.save(layoutKey, layout);
        opacity = Math.max(0.2f, Math.min(1.0f, opacityPercent / 100.0f));
        haptics = vibrate;
        layoutControls(getWidth(), getHeight());
        reportSettings();
        invalidate();
    }

    /** From General's row: every control back where the game puts it on this shape of screen. */
    void resetPlaces() {
        Arrays.fill(layout.x, Float.NaN);
        Arrays.fill(layout.y, Float.NaN);
        layouts.save(layoutKey, layout);
        layoutControls(getWidth(), getHeight());
        invalidate();
    }

    // Arranging the controls: the icons can be dragged; the game's window asks Save (the places kept for this shape of
    // screen), Reset (every control where the game places it, kept with Save) or Cancel (the places it began with). A
    // finger that misses every control is the pointer on that window.
    void startArranging() {
        arranging = true;
        selected = -1;
        dragPointer = -1;
        System.arraycopy(layout.x, 0, arrangedX, 0, TouchLayouts.ITEMS);
        System.arraycopy(layout.y, 0, arrangedY, 0, TouchLayouts.ITEMS);
        // A finger or button held when this began lets go.
        Arrays.fill(targets, TARGET_NONE);
        Arrays.fill(pointerBits, 0);
        stickPointer = -1;
        publish(0, 0.0f, 0.0f);
        placeHome();
        invalidate();
    }

    boolean isArranging() {
        return arranging;
    }

    void finishArranging(boolean save) {
        arranging = false;
        selected = -1;
        if (save) {
            layouts.save(layoutKey, layout);
        } else {
            System.arraycopy(arrangedX, 0, layout.x, 0, TouchLayouts.ITEMS);
            System.arraycopy(arrangedY, 0, layout.y, 0, TouchLayouts.ITEMS);
            layoutControls(getWidth(), getHeight());
        }
        placeHome();
        listener.arrangingDone();
        invalidate();
    }

    // Every control where the game places it on this shape of screen; Save keeps that.
    void resetArranged() {
        if (!arranging) {
            return;
        }
        Arrays.fill(layout.x, Float.NaN);
        Arrays.fill(layout.y, Float.NaN);
        selected = -1;
        dragPointer = -1;
        layoutControls(getWidth(), getHeight());
        invalidate();
    }

    // The control under a point: its place in a layout, or -1.
    private int itemAt(float x, float y) {
        int best = -1;
        float bestDistance = Float.MAX_VALUE;
        for (int i = 0; i < buttons.size(); ++i) {
            final Button button = buttons.get(i);
            final float distance = (float) Math.hypot(x - button.x, y - button.y) / (button.radius * 1.25f);
            if (distance <= 1.0f && distance < bestDistance) {
                best = i;
                bestDistance = distance;
            }
        }
        final float dpad = (float) Math.hypot(x - dpadX, y - dpadY) / (dpadRadius * 1.1f);
        if (dpad <= 1.0f && dpad < bestDistance) {
            best = ITEM_DPAD;
            bestDistance = dpad;
        }
        final float stick = (float) Math.hypot(x - stickRestX, y - stickRestY) / stickRadius;
        if (stick <= 1.0f && stick < bestDistance) {
            best = ITEM_STICK;
        }
        return best;
    }

    private float itemX(int item) {
        return item == ITEM_STICK ? stickRestX : item == ITEM_DPAD ? dpadX : buttons.get(item).x;
    }

    private float itemY(int item) {
        return item == ITEM_STICK ? stickRestY : item == ITEM_DPAD ? dpadY : buttons.get(item).y;
    }

    private float itemRadius(int item) {
        return item == ITEM_STICK ? stickRadius : item == ITEM_DPAD ? dpadRadius : buttons.get(item).radius;
    }

    private void moveItem(int item, float x, float y) {
        final float r = itemRadius(item);
        final float cx = Math.max(r, Math.min(getWidth() - r, x));
        final float cy = Math.max(r, Math.min(getHeight() - r, y));
        layout.x[item] = cx / getWidth();
        layout.y[item] = cy / getHeight();
        layoutControls(getWidth(), getHeight());
        invalidate();
    }

    // One finger at a time drags a control; a finger that lands on none is the pointer on the game's window.
    private void arrangeEvent(MotionEvent event) {
        final int action = event.getActionMasked();
        final int index = event.getActionIndex();
        final int id = event.getPointerId(index);
        switch (action) {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN: {
            if (id < 0 || id >= MAX_POINTERS) {
                return;
            }
            final float x = event.getX(index);
            final float y = event.getY(index);
            final int item = dragPointer < 0 ? itemAt(x, y) : -1;
            if (item >= 0) {
                selected = item;
                dragPointer = id;
                dragOffsetX = itemX(item) - x;
                dragOffsetY = itemY(item) - y;
                if (haptics) {
                    performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
                }
                invalidate();
                return;
            }
            if (dragPointer < 0) {
                targets[id] = TARGET_POINTER;
                listener.onPointer(0, id, x, y);
            }
            return;
        }
        case MotionEvent.ACTION_MOVE:
            for (int i = 0; i < event.getPointerCount(); ++i) {
                final int finger = event.getPointerId(i);
                if (finger == dragPointer) {
                    moveItem(selected, event.getX(i) + dragOffsetX, event.getY(i) + dragOffsetY);
                } else {
                    move(finger, event.getX(i), event.getY(i));
                }
            }
            return;
        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_POINTER_UP:
            if (id == dragPointer) {
                dragPointer = -1;
            } else {
                up(id, event.getX(index), event.getY(index));
            }
            return;
        case MotionEvent.ACTION_CANCEL:
            dragPointer = -1;
            for (int i = 0; i < event.getPointerCount(); ++i) {
                up(event.getPointerId(i), event.getX(i), event.getY(i));
            }
            return;
        default:
            return;
        }
    }

    // Only the controls over the game's window: the one last taken is ringed in white, as the game marks what it has
    // chosen.
    private void drawArranging(Canvas canvas) {
        for (Button button : buttons) {
            drawButton(canvas, button, false);
        }
        drawDpad(canvas);
        drawStick(canvas);
        if (selected >= 0) {
            final float x = itemX(selected);
            final float y = itemY(selected);
            final float r = itemRadius(selected);
            stroke.setColor(0xFFFFFFFF);
            stroke.setAlpha(70);
            stroke.setStrokeWidth(7 * dp);
            canvas.drawCircle(x, y, r + 6 * dp, stroke);
            stroke.setAlpha(dragPointer >= 0 ? 255 : 210);
            stroke.setStrokeWidth(2.5f * dp);
            canvas.drawCircle(x, y, r + 6 * dp, stroke);
            stroke.setStrokeWidth(1.6f * dp);
        }
    }

    private static void place(Button button, float x, float y, float radius) {
        button.x = x;
        button.y = y;
        button.radius = radius;
    }

    private static void placeOnArc(Button button, Button centre, float reach, float degrees, float radius) {
        final double angle = Math.toRadians(degrees);
        place(button, (float) (centre.x + reach * Math.cos(angle)), (float) (centre.y - reach * Math.sin(angle)), radius);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (arranging) {
            arrangeEvent(event);
            return true;
        }
        final int action = event.getActionMasked();
        switch (action) {
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_POINTER_DOWN: {
            final int index = event.getActionIndex();
            down(event.getPointerId(index), event.getX(index), event.getY(index));
            break;
        }
        case MotionEvent.ACTION_MOVE:
            for (int i = 0; i < event.getPointerCount(); ++i) {
                move(event.getPointerId(i), event.getX(i), event.getY(i));
            }
            break;
        case MotionEvent.ACTION_UP:
        case MotionEvent.ACTION_POINTER_UP: {
            final int index = event.getActionIndex();
            up(event.getPointerId(index), event.getX(index), event.getY(index));
            break;
        }
        case MotionEvent.ACTION_CANCEL:
            for (int i = 0; i < event.getPointerCount(); ++i) {
                up(event.getPointerId(i), event.getX(i), event.getY(i));
            }
            break;
        default:
            break;
        }
        return true;
    }

    private void down(int id, float x, float y) {
        if (id < 0 || id >= MAX_POINTERS) {
            return;
        }
        int target = TARGET_POINTER;
        syncMenuContext();
        if (shown) {
            final int button = buttonAt(x, y);
            if (button >= 0) {
                target = button;
            } else if (pointerScreen) {
                target = TARGET_POINTER;
            } else if (listener.touchTarget(x, y)) {
                target = TARGET_POINTER;
            } else if (dpadBits(x, y) != 0 || Math.hypot(x - dpadX, y - dpadY) <= dpadRadius * 1.2f) {
                target = TARGET_DPAD;
            } else if (stickPointer < 0 && !listener.titleScreen() && inStickZone(x, y)) {
                target = TARGET_STICK;
                stickPointer = id;
                // The stick centres under the thumb, kept whole on the screen.
                stickBaseX = Math.min(Math.max(x, stickRadius + Math.max(safe.left, 0) + 8 * dp),
                        getWidth() - stickRadius - Math.max(safe.right, 0) - 8 * dp);
                stickBaseY = Math.min(y, getHeight() - Math.max(safe.bottom, 0) - stickRadius - 8 * dp);
                stickKnobX = x;
                stickKnobY = y;
            }
        }
        targets[id] = target;
        if (target == TARGET_POINTER) {
            listener.onPointer(0, id, x, y);
            return;
        }
        zLeft[id] = false;
        zDownMs[id] = SystemClock.uptimeMillis();
        update(id, x, y);
    }

    private void move(int id, float x, float y) {
        final int target = id >= 0 && id < MAX_POINTERS ? targets[id] : TARGET_NONE;
        if (target == TARGET_NONE) {
            return;
        }
        if (target == TARGET_POINTER) {
            listener.onPointer(1, id, x, y);
            return;
        }
        update(id, x, y);
    }

    private void up(int id, float x, float y) {
        final int target = id >= 0 && id < MAX_POINTERS ? targets[id] : TARGET_NONE;
        if (target == TARGET_NONE) {
            return;
        }
        targets[id] = TARGET_NONE;
        if (target == TARGET_POINTER) {
            listener.onPointer(2, id, x, y);
            return;
        }
        pointerBits[id] = 0;
        if (target == TARGET_STICK) {
            stickPointer = -1;
        }
        recompute();
    }

    // A control finger moved or landed: what it holds now.
    private void update(int id, float x, float y) {
        final int target = targets[id];
        int bits = 0;
        if (target == TARGET_STICK) {
            stickKnobX = x;
            stickKnobY = y;
        } else if (target == TARGET_DPAD) {
            bits = dpadBits(x, y);
        } else {
            // A button finger slides: it holds the button it is over, if any.
            final int over = buttonAt(x, y);
            bits = over >= 0 ? buttons.get(over).bit : 0;
            if (target >= 0 && target < buttons.size() && buttons.get(target).bit == Z) {
                bits = slideFromZ(id, over, bits);
            }
        }
        pointerBits[id] = bits;
        recompute();
    }

    // Z held and A pressed is the game's long jump while Mario still runs and its crouch jump once the crouch has stopped
    // him, about nine frames after Z. A thumb's slide takes longer than that, so a quick slide with the stick pushed lets
    // go of Z on the way (Mario keeps running) and gives it back on the button it reaches, both at once: the long jump.
    // Held on Z first, or standing still, the slide keeps Z all the way: the crouch jump.
    private int slideFromZ(int id, int over, int overBits) {
        if (over >= 0 && buttons.get(over).bit == Z) {
            zLeft[id] = false;
            return Z;
        }
        if (!zLeft[id]) {
            zLeft[id] = true;
            zQuick[id] = SystemClock.uptimeMillis() - zDownMs[id] <= Z_QUICK_SLIDE_MS && Math.hypot(stickX, stickY) > Z_QUICK_SLIDE_STICK;
        }
        return over >= 0 || !zQuick[id] ? overBits | Z : 0;
    }

    private void recompute() {
        int bits = 0;
        for (int held : pointerBits) {
            bits |= held;
        }
        float sx = 0.0f;
        float sy = 0.0f;
        if (stickPointer >= 0) {
            final float dx = (stickKnobX - stickBaseX) / stickRadius;
            final float dy = (stickBaseY - stickKnobY) / stickRadius;
            final float length = (float) Math.hypot(dx, dy);
            if (length > STICK_DEAD_ZONE) {
                // A radial dead zone, the rest of the travel to the rim rescaled to 0..1.
                final float magnitude = (Math.min(length, 1.0f) - STICK_DEAD_ZONE) / (1.0f - STICK_DEAD_ZONE);
                sx = dx / length * magnitude;
                sy = dy / length * magnitude;
            }
        }
        publish(bits, sx, sy);
    }

    private void publish(int bits, float sx, float sy) {
        final int pressed = bits & ~heldButtons;
        if (pressed != 0 && haptics) {
            performHapticFeedback(HapticFeedbackConstants.VIRTUAL_KEY);
        }
        if (bits != heldButtons || sx != stickX || sy != stickY) {
            heldButtons = bits;
            stickX = sx;
            stickY = sy;
            listener.onControls(bits, sx, sy);
            invalidate();
        }
    }

    private boolean inStickZone(float x, float y) {
        if (listener.menuContext()) {
            return Math.hypot(x - stickRestX, y - stickRestY) <= stickRadius * 1.15f;
        }
        final boolean side = stickOnLeft ? x < stickZoneRight : x > getWidth() - stickZoneRight;
        return side && y > stickZoneTop;
    }

    private int buttonAt(float x, float y) {
        int best = -1;
        float bestDistance = Float.MAX_VALUE;
        for (int i = 0; i < buttons.size(); ++i) {
            if (pointerScreen && i != HOME_BUTTON) {
                continue;
            }
            final Button button = buttons.get(i);
            final float distance = (float) Math.hypot(x - button.x, y - button.y);
            // A little beyond the drawn circle: thumbs land wide.
            if (distance <= button.radius * 1.25f && distance / button.radius < bestDistance) {
                best = i;
                bestDistance = distance / button.radius;
            }
        }
        return best;
    }

    // Eight directions: the arm under the finger, both arms on a diagonal.
    private int dpadBits(float x, float y) {
        final float dx = x - dpadX;
        final float dy = y - dpadY;
        final double distance = Math.hypot(dx, dy);
        if (distance < dpadRadius * 0.22f || distance > dpadRadius * 1.35f) {
            return 0;
        }
        final double angle = Math.toDegrees(Math.atan2(-dy, dx));
        final double a = angle < 0 ? angle + 360.0 : angle;
        int bits = 0;
        if (a < 67.5 || a >= 292.5) bits |= RIGHT;
        if (a >= 22.5 && a < 157.5) bits |= UP;
        if (a >= 112.5 && a < 247.5) bits |= LEFT;
        if (a >= 202.5 && a < 337.5) bits |= DOWN;
        return bits;
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (arranging) {
            stroke.setStrokeWidth(1.6f * dp);
            drawArranging(canvas);
            return;
        }
        if (!shown) {
            return;
        }
        stroke.setStrokeWidth(1.6f * dp);
        if (pointerScreen) {
            final Button home = buttons.get(HOME_BUTTON);
            drawButton(canvas, home, (heldButtons & home.bit) != 0);
            return;
        }
        for (Button button : buttons) {
            drawButton(canvas, button, (heldButtons & button.bit) != 0);
        }
        drawDpad(canvas);
        drawStick(canvas);
    }

    private int alpha(float value) {
        return Math.round(255 * Math.max(0.0f, Math.min(1.0f, value * opacity)));
    }

    private void drawButton(Canvas canvas, Button button, boolean held) {
        final float r = held ? button.radius * 0.94f : button.radius;
        fill.setColor(held ? 0xFFFFFFFF : 0xFF0A0C1C);
        fill.setAlpha(alpha(held ? 0.42f : 0.38f));
        canvas.drawCircle(button.x, button.y, r, fill);
        stroke.setColor(0xFFFFFFFF);
        stroke.setAlpha(alpha(held ? 0.95f : 0.62f));
        canvas.drawCircle(button.x, button.y, r, stroke);
        final int ink = held ? 0xFF0A0C1C : 0xFFFFFFFF;
        if (button.spinIcon) {
            drawSpin(canvas, button.x, button.y, r * 0.5f, ink);
        } else if (button.homeIcon) {
            drawHome(canvas, button.x, button.y, r * 0.48f, ink);
        } else {
            text.setColor(ink);
            text.setAlpha(alpha(0.95f));
            final float size = text.getTextSize();
            text.setTextSize(Math.min(size, r * (button.label.length() > 1 ? 0.7f : 0.95f)));
            final Paint.FontMetrics metrics = text.getFontMetrics();
            canvas.drawText(button.label, button.x, button.y - (metrics.ascent + metrics.descent) / 2.0f, text);
            text.setTextSize(size);
        }
    }

    // Two arrows chasing round a circle: the shake that spins.
    private void drawSpin(Canvas canvas, float x, float y, float r, int ink) {
        line.setColor(ink);
        line.setAlpha(alpha(0.95f));
        line.setStrokeWidth(Math.max(2.0f * dp, r * 0.16f));
        rect.set(x - r, y - r, x + r, y + r);
        for (int k = 0; k < 2; ++k) {
            final float start = k * 180.0f + 20.0f;
            canvas.drawArc(rect, start, 130.0f, false, line);
            final double end = Math.toRadians(start + 130.0f);
            final float ex = (float) (x + r * Math.cos(end));
            final float ey = (float) (y + r * Math.sin(end));
            final double tangent = end + Math.PI / 2.0;
            final float head = r * 0.42f;
            path.reset();
            path.moveTo(ex + (float) (head * Math.cos(tangent + 2.6)), ey + (float) (head * Math.sin(tangent + 2.6)));
            path.lineTo(ex, ey);
            path.lineTo(ex + (float) (head * Math.cos(tangent - 2.6)), ey + (float) (head * Math.sin(tangent - 2.6)));
            canvas.drawPath(path, line);
        }
    }

    private void drawHome(Canvas canvas, float x, float y, float r, int ink) {
        fill.setColor(ink);
        fill.setAlpha(alpha(0.95f));
        path.reset();
        path.moveTo(x, y - r);
        path.lineTo(x + r, y - r * 0.05f);
        path.lineTo(x + r * 0.68f, y - r * 0.05f);
        path.lineTo(x + r * 0.68f, y + r * 0.85f);
        path.lineTo(x + r * 0.18f, y + r * 0.85f);
        path.lineTo(x + r * 0.18f, y + r * 0.3f);
        path.lineTo(x - r * 0.18f, y + r * 0.3f);
        path.lineTo(x - r * 0.18f, y + r * 0.85f);
        path.lineTo(x - r * 0.68f, y + r * 0.85f);
        path.lineTo(x - r * 0.68f, y - r * 0.05f);
        path.lineTo(x - r, y - r * 0.05f);
        path.close();
        canvas.drawPath(path, fill);
    }

    private void drawDpad(Canvas canvas) {
        final float arm = dpadRadius * 0.38f;
        final float r = dpadRadius;
        path.reset();
        path.moveTo(dpadX - arm, dpadY - r);
        path.lineTo(dpadX + arm, dpadY - r);
        path.lineTo(dpadX + arm, dpadY - arm);
        path.lineTo(dpadX + r, dpadY - arm);
        path.lineTo(dpadX + r, dpadY + arm);
        path.lineTo(dpadX + arm, dpadY + arm);
        path.lineTo(dpadX + arm, dpadY + r);
        path.lineTo(dpadX - arm, dpadY + r);
        path.lineTo(dpadX - arm, dpadY + arm);
        path.lineTo(dpadX - r, dpadY + arm);
        path.lineTo(dpadX - r, dpadY - arm);
        path.lineTo(dpadX - arm, dpadY - arm);
        path.close();
        fill.setColor(0xFF0A0C1C);
        fill.setAlpha(alpha(0.38f));
        canvas.drawPath(path, fill);
        stroke.setColor(0xFFFFFFFF);
        stroke.setAlpha(alpha(0.62f));
        canvas.drawPath(path, stroke);
        // The held arms, lit.
        fill.setColor(0xFFFFFFFF);
        fill.setAlpha(alpha(0.45f));
        final float inner = arm * 0.82f;
        if ((heldButtons & UP) != 0) canvas.drawRect(dpadX - inner, dpadY - r + 2 * dp, dpadX + inner, dpadY - arm, fill);
        if ((heldButtons & DOWN) != 0) canvas.drawRect(dpadX - inner, dpadY + arm, dpadX + inner, dpadY + r - 2 * dp, fill);
        if ((heldButtons & LEFT) != 0) canvas.drawRect(dpadX - r + 2 * dp, dpadY - inner, dpadX - arm, dpadY + inner, fill);
        if ((heldButtons & RIGHT) != 0) canvas.drawRect(dpadX + arm, dpadY - inner, dpadX + r - 2 * dp, dpadY + inner, fill);
    }

    private void drawStick(Canvas canvas) {
        final boolean held = stickPointer >= 0;
        final float bx = held ? stickBaseX : stickRestX;
        final float by = held ? stickBaseY : stickRestY;
        fill.setColor(0xFF0A0C1C);
        fill.setAlpha(alpha(held ? 0.32f : 0.22f));
        canvas.drawCircle(bx, by, stickRadius, fill);
        stroke.setColor(0xFFFFFFFF);
        stroke.setAlpha(alpha(held ? 0.7f : 0.4f));
        canvas.drawCircle(bx, by, stickRadius, stroke);
        // The knob under the thumb, kept inside the rim.
        float kx = bx;
        float ky = by;
        if (held) {
            final float dx = stickKnobX - bx;
            final float dy = stickKnobY - by;
            final float length = (float) Math.hypot(dx, dy);
            final float k = length > stickRadius ? stickRadius / length : 1.0f;
            kx = bx + dx * k;
            ky = by + dy * k;
        }
        fill.setColor(0xFFFFFFFF);
        fill.setAlpha(alpha(held ? 0.72f : 0.45f));
        canvas.drawCircle(kx, ky, stickRadius * 0.44f, fill);
    }
}
