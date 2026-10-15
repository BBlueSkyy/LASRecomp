package com.bblueskyy.lasrecomp;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.Set;
import org.libsdl.app.SDLActivity;

/** Multitouch controls emit the runtime's default keyboard bindings through SDL. */
final class TouchControls extends View {
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ArrayList<Control> buttons = new ArrayList<>();
    private final Set<Integer> held = new HashSet<>();
    private boolean visible = true;
    private float unit, leftX, leftY, rightX, rightY;
    private RectF toggle = new RectF();

    private static final class Control {
        final String text;
        final int key;
        final RectF rect;
        Control(String text, int key, RectF rect) { this.text = text; this.key = key; this.rect = rect; }
    }

    TouchControls(Context context) {
        super(context);
        setFocusable(false);
        setContentDescription("Controles de toque: dois analógicos, direcional e botões do jogo");
    }

    @Override protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        releaseAll();
        unit = Math.min(height / 8.5f, width / 20f);
        buttons.clear();
        leftX = 2.1f * unit;
        leftY = height - 2.1f * unit;
        rightX = width - 5.4f * unit;
        rightY = height - 1.8f * unit;
        add("L", KeyEvent.KEYCODE_Q, unit, unit);
        add("ZL", KeyEvent.KEYCODE_1, 3f * unit, unit);
        add("R", KeyEvent.KEYCODE_E, width - unit, unit);
        add("ZR", KeyEvent.KEYCODE_3, width - 3f * unit, unit);
        add("−", KeyEvent.KEYCODE_DEL, width / 2f - 1.5f * unit, unit);
        add("+", KeyEvent.KEYCODE_ENTER, width / 2f + 1.5f * unit, unit);
        add("☰", KeyEvent.KEYCODE_F1, width / 2f, unit);
        add("L3", KeyEvent.KEYCODE_F, width / 2f - .7f * unit, height - .8f * unit);
        add("R3", KeyEvent.KEYCODE_NUMPAD_0, width / 2f + .7f * unit, height - .8f * unit);
        add("X", KeyEvent.KEYCODE_L, width - 2f * unit, height - 3.5f * unit);
        add("Y", KeyEvent.KEYCODE_I, width - 3.2f * unit, height - 2.3f * unit);
        add("A", KeyEvent.KEYCODE_K, width - .8f * unit, height - 2.3f * unit);
        add("B", KeyEvent.KEYCODE_J, width - 2f * unit, height - 1.1f * unit);
        float dx = 5.5f * unit, dy = height - 2.1f * unit;
        add("↑", KeyEvent.KEYCODE_DPAD_UP, dx, dy - unit);
        add("←", KeyEvent.KEYCODE_DPAD_LEFT, dx - unit, dy);
        add("→", KeyEvent.KEYCODE_DPAD_RIGHT, dx + unit, dy);
        add("↓", KeyEvent.KEYCODE_DPAD_DOWN, dx, dy + unit);
        toggle.set(width / 2f - 1.5f * unit, 2f * unit,
                width / 2f + 1.5f * unit, 2.7f * unit);
    }

    private void add(String text, int key, float x, float y) {
        float radius = .44f * unit;
        buttons.add(new Control(text, key, new RectF(x - radius, y - radius,
                x + radius, y + radius)));
    }

    @Override protected void onDraw(Canvas canvas) {
        paint.setColor(Color.argb(100, 25, 30, 38));
        canvas.drawRoundRect(toggle, 10, 10, paint);
        text(canvas, visible ? "Ocultar toque" : "Mostrar toque", toggle.centerX(), toggle.centerY(), .28f * unit);
        if (!visible) return;
        for (Control control : buttons) {
            paint.setColor(held.contains(control.key) ? Color.argb(180, 95, 170, 210)
                    : Color.argb(100, 25, 30, 38));
            canvas.drawRoundRect(control.rect, unit, unit, paint);
            text(canvas, control.text, control.rect.centerX(), control.rect.centerY(), .4f * unit);
        }
        stick(canvas, leftX, leftY, "L");
        stick(canvas, rightX, rightY, "R");
    }

    private void stick(Canvas canvas, float x, float y, String label) {
        paint.setColor(Color.argb(85, 25, 30, 38));
        canvas.drawCircle(x, y, 1.45f * unit, paint);
        paint.setColor(Color.argb(120, 225, 230, 235));
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(2);
        canvas.drawCircle(x, y, .48f * unit, paint);
        paint.setStyle(Paint.Style.FILL);
        text(canvas, label, x, y, .35f * unit);
    }

    private void text(Canvas canvas, String label, float x, float y, float size) {
        paint.setColor(Color.argb(210, 255, 255, 255));
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(size);
        canvas.drawText(label, x, y - (paint.ascent() + paint.descent()) / 2, paint);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN && toggle.contains(event.getX(), event.getY())) {
            releaseAll();
            visible = !visible;
            performClick();
            invalidate();
            return true;
        }
        if (!visible) return false;
        // Leave the unobstructed game/menu area to SDL's own touch/mouse handler.
        if (action == MotionEvent.ACTION_DOWN && !hitControl(event.getX(), event.getY())) return false;
        Set<Integer> next = new HashSet<>();
        if (action != MotionEvent.ACTION_UP && action != MotionEvent.ACTION_CANCEL) {
            for (int pointer = 0; pointer < event.getPointerCount(); pointer++) {
                if (action == MotionEvent.ACTION_POINTER_UP && pointer == event.getActionIndex()) continue;
                float x = event.getX(pointer), y = event.getY(pointer);
                for (Control control : buttons)
                    if (control.rect.contains(x, y)) next.add(control.key);
                stickKeys(next, x, y, leftX, leftY, KeyEvent.KEYCODE_W, KeyEvent.KEYCODE_S,
                        KeyEvent.KEYCODE_A, KeyEvent.KEYCODE_D);
                stickKeys(next, x, y, rightX, rightY, KeyEvent.KEYCODE_NUMPAD_8, KeyEvent.KEYCODE_NUMPAD_5,
                        KeyEvent.KEYCODE_NUMPAD_4, KeyEvent.KEYCODE_NUMPAD_6);
            }
        }
        for (int key : held) if (!next.contains(key)) SDLActivity.onNativeKeyUp(key);
        for (int key : next) if (!held.contains(key)) SDLActivity.onNativeKeyDown(key);
        held.clear();
        held.addAll(next);
        invalidate();
        return true;
    }

    private boolean hitControl(float x, float y) {
        for (Control control : buttons) if (control.rect.contains(x, y)) return true;
        float lx = x - leftX, ly = y - leftY, rx = x - rightX, ry = y - rightY;
        float radiusSquared = 1.45f * 1.45f * unit * unit;
        return lx * lx + ly * ly <= radiusSquared || rx * rx + ry * ry <= radiusSquared;
    }

    private void stickKeys(Set<Integer> keys, float x, float y, float cx, float cy,
            int up, int down, int left, int right) {
        float dx = x - cx, dy = y - cy;
        if (dx * dx + dy * dy > 1.45f * 1.45f * unit * unit) return;
        if (dx < -.3f * unit) keys.add(left);
        if (dx > .3f * unit) keys.add(right);
        if (dy < -.3f * unit) keys.add(up);
        if (dy > .3f * unit) keys.add(down);
    }

    @Override public boolean performClick() { return super.performClick(); }

    void releaseAll() {
        for (int key : held) SDLActivity.onNativeKeyUp(key);
        held.clear();
        invalidate();
    }
}
