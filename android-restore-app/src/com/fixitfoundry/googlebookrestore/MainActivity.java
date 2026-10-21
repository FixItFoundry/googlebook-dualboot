package com.fixitfoundry.googlebookrestore;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.AssetFileDescriptor;
import android.content.res.AssetManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RadialGradient;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.Bundle;
import android.net.Uri;
import android.database.Cursor;
import android.provider.DocumentsContract;
import android.content.ContentResolver;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedInputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.Map;

public class MainActivity extends Activity {
    private static final String ACTION_USB_PERMISSION = "com.fixitfoundry.googlebookrestore.USB_PERMISSION";
    private static final int FASTBOOT_CLASS = 0xFF;
    private static final int FASTBOOT_SUBCLASS = 0x42;
    private static final int FASTBOOT_PROTOCOL = 0x03;
    private static final int GOOGLE_VID = 0x18D1;

    private final Handler uiHandler = new Handler(Looper.getMainLooper());
    private UsbManager usbManager;
    private boolean useUsbMode = true;
    private TextView tabUsb;
    private TextView tabTcp;
    private EditText etTcpHost;
    private Uri backupTree;
    private Button btnChooseBackup;
    private CheckBox cbAutoReboot;
    private TextView tvLog;
    private ScrollView scrollLog;
    private Button btnCheck;
    private Button btnSwitchSlotB;
    private Button btnRestoreSlotB;

    private final BroadcastReceiver usbReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (ACTION_USB_PERMISSION.equals(intent.getAction())) {
                UsbDevice dev = intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
                boolean granted = intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
                if (granted && dev != null) {
                    appendLog("[USB] Permission granted for " + dev.getDeviceName());
                } else {
                    appendLog("[USB] Permission denied.");
                }
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window w = getWindow();
        if (w != null) {
            w.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
            w.setStatusBarColor(Color.BLACK);
            w.setNavigationBarColor(Color.BLACK);
        }

        usbManager = (UsbManager) getSystemService(Context.USB_SERVICE);

        IntentFilter filter = new IntentFilter(ACTION_USB_PERMISSION);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(usbReceiver, filter, null, null, 4 /* Context.RECEIVER_NOT_EXPORTED */);
        } else {
            registerReceiver(usbReceiver, filter);
        }

        buildUi();
        appendLog("Googlebook 14 (Moonstone) Ready.");
        String savedTree = getPreferences(MODE_PRIVATE).getString("backupTree", null);
        if (savedTree != null) backupTree = Uri.parse(savedTree);
        appendLog("Choose a backup folder, or use bundled images. Only verified Linux Slot B can be restored.");
        appendLog("Android OTA recovery is unsupported. Fastboot fetch support is required.");
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == 1 && result == RESULT_OK && data != null && data.getData() != null) {
            try {
                Uri tree = data.getData();
                getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                backupTree = tree;
                getPreferences(MODE_PRIVATE).edit().putString("backupTree", tree.toString()).apply();
                appendLog("Backup folder selected. Tap Restore when ready.");
            } catch (SecurityException e) {
                appendLog("[ERROR] Cannot retain folder access: " + e.getMessage());
            }
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        try {
            unregisterReceiver(usbReceiver);
        } catch (Exception ignored) {
        }
    }

    private void buildUi() {
        FrameLayout frame = new FrameLayout(this);
        frame.setBackgroundColor(Color.BLACK);

        // Background Aurora + Halftone Dot Wave matching googlebook.google
        AuroraHalftoneView bgView = new AuroraHalftoneView(this);
        frame.addView(bgView, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        int padH = dp(20);
        int padV = dp(18);
        root.setPadding(padH, padV, padH, padV);

        Typeface googleSans = Typeface.create("sans-serif-medium", Typeface.NORMAL);
        Typeface googleSansBold = Typeface.create("sans-serif-medium", Typeface.BOLD);
        Typeface googleSansRegular = Typeface.create("sans-serif", Typeface.NORMAL);

        // Top Bar: 4-Color Google 'G' + "Googlebook" wordmark
        LinearLayout brandRow = new LinearLayout(this);
        brandRow.setOrientation(LinearLayout.HORIZONTAL);
        brandRow.setGravity(Gravity.CENTER_VERTICAL);
        GoogleGView gLogo = new GoogleGView(this);
        LinearLayout.LayoutParams gLp = new LinearLayout.LayoutParams(dp(26), dp(26));
        gLp.rightMargin = dp(10);
        brandRow.addView(gLogo, gLp);

        TextView brandText = new TextView(this);
        brandText.setText("Googlebook");
        brandText.setTextColor(Color.WHITE);
        brandText.setTextSize(21);
        brandText.setTypeface(googleSans);
        brandText.setLetterSpacing(-0.02f);
        brandRow.addView(brandText);
        root.addView(brandRow, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));

        // Floating Pill Segmented Nav Bar (matches googlebook.google top nav pill)
        LinearLayout navPill = new LinearLayout(this);
        navPill.setOrientation(LinearLayout.HORIZONTAL);
        navPill.setGravity(Gravity.CENTER);
        navPill.setPadding(dp(4), dp(4), dp(4), dp(4));
        navPill.setBackground(makeRoundedDrawable("#1A1B1E", "#2C2E33", dp(999), dp(1)));
        LinearLayout.LayoutParams navLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        navLp.topMargin = dp(14);
        navPill.setLayoutParams(navLp);

        tabUsb = makePillTab("USB-C Cable", googleSans);
        tabTcp = makePillTab("TCP Fastboot", googleSans);
        navPill.addView(tabUsb);
        navPill.addView(tabTcp);
        root.addView(navPill);

        etTcpHost = new EditText(this);
        etTcpHost.setHint("TCP Fastboot IP:Port (e.g. 192.168.8.50:5554)");
        etTcpHost.setText("192.168.8.50:5554");
        etTcpHost.setTextColor(Color.WHITE);
        etTcpHost.setHintTextColor(Color.parseColor("#6E7681"));
        etTcpHost.setTextSize(13);
        etTcpHost.setSingleLine(true);
        etTcpHost.setPadding(dp(16), dp(10), dp(16), dp(10));
        etTcpHost.setBackground(makeRoundedDrawable("#14161A", "#2C2E33", dp(999), dp(1)));
        etTcpHost.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        LinearLayout.LayoutParams tcpLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        tcpLp.topMargin = dp(10);
        etTcpHost.setLayoutParams(tcpLp);
        etTcpHost.setVisibility(View.GONE);
        root.addView(etTcpHost);

        tabUsb.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                selectTransportMode(true);
            }
        });
        tabTcp.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                selectTransportMode(false);
            }
        });
        selectTransportMode(true);

        // Hero Headline ("Stay in the loop." style from googlebook.google)
        TextView heroTitle = new TextView(this);
        heroTitle.setText("Stay in control.");
        heroTitle.setTextColor(Color.WHITE);
        heroTitle.setTextSize(30);
        heroTitle.setTypeface(googleSans);
        heroTitle.setLetterSpacing(-0.03f);
        heroTitle.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams heroLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        heroLp.topMargin = dp(16);
        root.addView(heroTitle, heroLp);

        TextView heroSub = new TextView(this);
        heroSub.setText("Restore an existing Linux Slot B. Android OTA recovery is unsupported.");
        heroSub.setTextColor(Color.parseColor("#9AA0A6"));
        heroSub.setTextSize(13);
        heroSub.setTypeface(googleSansRegular);
        heroSub.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams subLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        subLp.topMargin = dp(4);
        subLp.bottomMargin = dp(14);
        root.addView(heroSub, subLp);

        // Primary White Pill CTA Button (matches "Get updates" & "Shop" pill on googlebook.google)
        btnRestoreSlotB = makePillButton(
                "Flash & Restore Linux Slot B",
                "#FFFFFF", "#000000", "#FFFFFF", googleSansBold);
        btnRestoreSlotB.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                runTask("restore");
            }
        });
        root.addView(btnRestoreSlotB);

        // Secondary Dark Frosted Pill Buttons
        LinearLayout secondaryRow = new LinearLayout(this);
        secondaryRow.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams secRowLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT);
        secRowLp.topMargin = dp(10);
        secondaryRow.setLayoutParams(secRowLp);

        btnSwitchSlotB = makePillButton(
                "Set Slot B & Reboot",
                "#181A1F", "#FFFFFF", "#30333B", googleSans);
        LinearLayout.LayoutParams b1Lp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f);
        b1Lp.rightMargin = dp(6);
        btnSwitchSlotB.setLayoutParams(b1Lp);
        btnSwitchSlotB.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                runTask("switch_b");
            }
        });

        btnCheck = makePillButton(
                "Check Status",
                "#181A1F", "#E8EAED", "#30333B", googleSans);
        LinearLayout.LayoutParams b2Lp = new LinearLayout.LayoutParams(
                0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f);
        b2Lp.leftMargin = dp(6);
        btnCheck.setLayoutParams(b2Lp);
        btnCheck.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                runTask("check");
            }
        });

        secondaryRow.addView(btnSwitchSlotB);
        secondaryRow.addView(btnCheck);
        root.addView(secondaryRow);

        // Options & Console Frosted Card
        LinearLayout card = new LinearLayout(this);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(14), dp(12), dp(14), dp(12));
        card.setBackground(makeRoundedDrawable("#CC101216", "#262932", dp(20), dp(1)));
        LinearLayout.LayoutParams cardLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f);
        cardLp.topMargin = dp(14);
        card.setLayoutParams(cardLp);

        btnChooseBackup = makePillButton("Choose backup folder", "#181A1F", "#FFFFFF", "#30333B", googleSans);
        btnChooseBackup.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
                startActivityForResult(intent, 1);
            }
        });
        card.addView(btnChooseBackup);

        cbAutoReboot = new CheckBox(this);
        cbAutoReboot.setText("Reboot Googlebook automatically when complete");
        cbAutoReboot.setTextColor(Color.parseColor("#E8EAED"));
        cbAutoReboot.setTextSize(12.5f);
        cbAutoReboot.setTypeface(googleSansRegular);
        cbAutoReboot.setChecked(true);
        card.addView(cbAutoReboot);

        scrollLog = new ScrollView(this);
        LinearLayout.LayoutParams scrollLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1.0f);
        scrollLp.topMargin = dp(8);
        scrollLog.setLayoutParams(scrollLp);
        scrollLog.setBackground(makeRoundedDrawable("#08090C", "#1C1E24", dp(14), dp(1)));

        tvLog = new TextView(this);
        tvLog.setTypeface(Typeface.MONOSPACE);
        tvLog.setTextSize(11.5f);
        tvLog.setTextColor(Color.parseColor("#8AB4F8"));
        tvLog.setPadding(dp(12), dp(10), dp(12), dp(10));
        scrollLog.addView(tvLog);
        card.addView(scrollLog);

        root.addView(card);
        frame.addView(root, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        setContentView(frame);
    }

    private TextView makePillTab(String text, Typeface tf) {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTypeface(tf);
        tv.setTextSize(13);
        tv.setPadding(dp(18), dp(8), dp(18), dp(8));
        tv.setGravity(Gravity.CENTER);
        return tv;
    }

    private void selectTransportMode(boolean usb) {
        useUsbMode = usb;
        if (usb) {
            tabUsb.setBackground(makeRoundedDrawable("#2C2E35", "#3C4043", dp(999), dp(1)));
            tabUsb.setTextColor(Color.WHITE);
            tabTcp.setBackgroundColor(Color.TRANSPARENT);
            tabTcp.setTextColor(Color.parseColor("#9AA0A6"));
            etTcpHost.setVisibility(View.GONE);
        } else {
            tabTcp.setBackground(makeRoundedDrawable("#2C2E35", "#3C4043", dp(999), dp(1)));
            tabTcp.setTextColor(Color.WHITE);
            tabUsb.setBackgroundColor(Color.TRANSPARENT);
            tabUsb.setTextColor(Color.parseColor("#9AA0A6"));
            etTcpHost.setVisibility(View.VISIBLE);
        }
    }

    private Button makePillButton(String text, String bgHex, String fgHex, String strokeHex, Typeface tf) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTypeface(tf);
        b.setTextSize(14);
        b.setTextColor(Color.parseColor(fgHex));
        b.setGravity(Gravity.CENTER);
        b.setPadding(dp(22), dp(13), dp(22), dp(13));
        b.setBackground(makeRoundedDrawable(bgHex, strokeHex, dp(999), dp(1)));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        b.setLayoutParams(lp);
        return b;
    }

    private GradientDrawable makeRoundedDrawable(String fillHex, String strokeHex, int radiusPx, int strokePx) {
        GradientDrawable gd = new GradientDrawable();
        gd.setColor(Color.parseColor(fillHex));
        gd.setCornerRadius(radiusPx);
        gd.setStroke(strokePx, Color.parseColor(strokeHex));
        return gd;
    }

    /**
     * Custom 4-color Google 'G' emblem matching the Googlebook boot/brand splash.
     */
    private static class GoogleGView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF arcRect = new RectF();

        GoogleGView(Context ctx) {
            super(ctx);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            float w = getWidth();
            float h = getHeight();
            float stroke = w * 0.21f;
            float pad = stroke * 0.6f;
            arcRect.set(pad, pad, w - pad, h - pad);

            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(stroke);
            paint.setStrokeCap(Paint.Cap.BUTT);

            // Red top arc
            paint.setColor(Color.parseColor("#EA4335"));
            canvas.drawArc(arcRect, 212f, 105f, false, paint);
            // Yellow left arc
            paint.setColor(Color.parseColor("#FBBC05"));
            canvas.drawArc(arcRect, 145f, 70f, false, paint);
            // Green bottom arc
            paint.setColor(Color.parseColor("#34A853"));
            canvas.drawArc(arcRect, 42f, 105f, false, paint);
            // Blue bottom-right arc
            paint.setColor(Color.parseColor("#4285F4"));
            canvas.drawArc(arcRect, -12f, 56f, false, paint);

            // Blue horizontal crossbar
            paint.setStyle(Paint.Style.FILL);
            float cy = h * 0.5f;
            float barHalf = stroke * 0.48f;
            canvas.drawRect(w * 0.50f, cy - barHalf, w - pad * 0.55f, cy + barHalf, paint);
        }
    }

    /**
     * Custom background view rendering the googlebook.google Aurora gradient + Halftone dot wave.
     */
    private static class AuroraHalftoneView extends View {
        private final Paint glowPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint dotPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

        AuroraHalftoneView(Context ctx) {
            super(ctx);
            dotPaint.setStyle(Paint.Style.FILL);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            int w = getWidth();
            int h = getHeight();
            if (w <= 0 || h <= 0) return;

            canvas.drawColor(Color.BLACK);

            // 4-color Aurora pools across the lower 60% of the canvas
            drawRadialPool(canvas, w * 0.12f, h * 0.82f, w * 0.72f, 0x553B82F6); // Google Blue
            drawRadialPool(canvas, w * 0.45f, h * 0.88f, w * 0.65f, 0x489B34EA); // Violet/Magenta
            drawRadialPool(canvas, w * 0.72f, h * 0.85f, w * 0.58f, 0x42E5A024); // Warm Gold
            drawRadialPool(canvas, w * 0.94f, h * 0.76f, w * 0.68f, 0x5228A766); // Emerald Green

            // Subtle halftone dot-matrix wave matching googlebook.google hero graphic
            int cols = 28;
            int rows = 18;
            float stepX = (float) w / cols;
            float startY = h * 0.46f;
            float stepY = (h - startY) / rows;

            for (int r = 0; r < rows; r++) {
                float v = (float) r / (rows - 1);
                for (int c = 0; c <= cols; c++) {
                    float u = (float) c / cols;
                    float wave = (float) Math.sin(u * 6.28318f - v * 2.2f) * stepY * 0.85f;
                    float cx = c * stepX + (r % 2 == 0 ? 0 : stepX * 0.5f);
                    float cy = startY + r * stepY + wave;
                    if (cy < startY || cy > h) continue;

                    int color = interpolateAuroraColor(u);
                    int alpha = (int) (Math.sin(v * 3.14159f) * 72f);
                    if (alpha <= 4) continue;
                    dotPaint.setColor((alpha << 24) | (color & 0x00FFFFFF));
                    float radius = (1.2f + v * 2.4f) * (w / 400f);
                    canvas.drawCircle(cx, cy, radius, dotPaint);
                }
            }
        }

        private void drawRadialPool(Canvas canvas, float cx, float cy, float radius, int centerColor) {
            RadialGradient rg = new RadialGradient(
                    cx, cy, Math.max(radius, 1f),
                    new int[]{centerColor, 0x00000000},
                    new float[]{0f, 1f},
                    Shader.TileMode.CLAMP);
            glowPaint.setShader(rg);
            canvas.drawCircle(cx, cy, radius, glowPaint);
        }

        private int interpolateAuroraColor(float u) {
            if (u < 0.35f) {
                return blendColor(0x4285F4, 0xA142F4, u / 0.35f);
            } else if (u < 0.68f) {
                return blendColor(0xA142F4, 0xFBBC04, (u - 0.35f) / 0.33f);
            } else {
                return blendColor(0xFBBC04, 0x34A853, (u - 0.68f) / 0.32f);
            }
        }

        private int blendColor(int c1, int c2, float t) {
            t = Math.max(0f, Math.min(1f, t));
            int r = (int) (((c1 >> 16) & 0xFF) * (1 - t) + ((c2 >> 16) & 0xFF) * t);
            int g = (int) (((c1 >> 8) & 0xFF) * (1 - t) + ((c2 >> 8) & 0xFF) * t);
            int b = (int) ((c1 & 0xFF) * (1 - t) + (c2 & 0xFF) * t);
            return (r << 16) | (g << 8) | b;
        }
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private void appendLog(final String msg) {
        uiHandler.post(new Runnable() {
            @Override
            public void run() {
                tvLog.append(msg + "\n");
                scrollLog.post(new Runnable() {
                    @Override
                    public void run() {
                        scrollLog.fullScroll(View.FOCUS_DOWN);
                    }
                });
            }
        });
    }

    private void setButtonsEnabled(final boolean enabled) {
        uiHandler.post(new Runnable() {
            @Override
            public void run() {
                btnChooseBackup.setEnabled(enabled);
                btnCheck.setEnabled(enabled);
                btnSwitchSlotB.setEnabled(enabled);
                btnRestoreSlotB.setEnabled(enabled);
                float alpha = enabled ? 1.0f : 0.55f;
                btnCheck.setAlpha(alpha);
                btnSwitchSlotB.setAlpha(alpha);
                btnRestoreSlotB.setAlpha(alpha);
            }
        });
    }

    private void runTask(final String action) {
        final boolean useUsb = useUsbMode;
        final String tcpTarget = etTcpHost.getText().toString().trim();
        final boolean autoReboot = cbAutoReboot.isChecked();

        if (useUsb) {
            UsbDevice dev = findFastbootUsbDevice();
            if (dev == null) {
                appendLog("[ERROR] No Fastboot USB device found. Ensure Googlebook is at the Fastboot screen and connected via USB-C.");
                return;
            }
            if (!usbManager.hasPermission(dev)) {
                appendLog("[USB] Requesting USB permission for " + dev.getDeviceName() + "...");
                int flags = Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0;
                PendingIntent pi = PendingIntent.getBroadcast(this, 0, new Intent(ACTION_USB_PERMISSION).setPackage(getPackageName()), flags);
                usbManager.requestPermission(dev, pi);
                return;
            }
        }

        setButtonsEnabled(false);
        new Thread(new Runnable() {
            @Override
            public void run() {
                FastbootTransport transport = null;
                try {
                    if (useUsb) {
                        transport = openUsbTransport();
                    } else {
                        transport = openTcpTransport(tcpTarget);
                    }
                    String product = transport.getVar("product");
                    String curSlot = normalizeSlot(transport.getVar("current-slot"));
                    appendLog("[Fastboot] Connected! product=" + product + ", current-slot=" + curSlot);

                    if ("check".equals(action)) {
                        String unlocked = transport.getVar("unlocked");
                        String maxDl = transport.getVar("max-download-size");
                        appendLog("[Fastboot] unlocked=" + unlocked + ", max-download-size=" + maxDl);
                    } else if ("switch_b".equals(action)) {
                        appendLog("[Fastboot] Setting active boot slot to 'b'...");
                        transport.sendCommand("set_active:b");
                        if (autoReboot) {
                            appendLog("[Fastboot] Rebooting into Slot B (googleboot-menu)...");
                            transport.sendCommand("reboot");
                        }
                        appendLog("[DONE] Active slot set to B!");
                    } else if ("restore".equals(action)) {
                        performSlotBRestore(transport, autoReboot);
                    }
                } catch (Exception e) {
                    appendLog("[ERROR] " + e.getMessage());
                } finally {
                    if (transport != null) {
                        transport.close();
                    }
                    setButtonsEnabled(true);
                }
            }
        }).start();
    }

    private String normalizeSlot(String raw) {
        if (raw == null) return "";
        String s = raw.trim().toLowerCase(Locale.US);
        if (s.startsWith("_")) s = s.substring(1);
        return s;
    }

    private void performSlotBRestore(FastbootTransport fb, boolean autoReboot) throws Exception {
        String[] partitions = {"boot_b", "init_boot_b", "vendor_boot_b"};
        ImageSource[] images = new ImageSource[partitions.length];
        for (int i = 0; i < partitions.length; i++) {
            images[i] = resolveImage(partitions[i] + ".img");
            try (InputStream in = images[i].open()) {
                BootImages.validate(partitions[i], in, images[i].size);
            }
            long capacity = Long.parseLong(fb.getVar("partition-size:" + partitions[i]).replaceFirst("^0[xX]", ""), 16);
            if (images[i].size > capacity) throw new IOException("Image exceeds " + partitions[i]);
        }
        long[] config = BootImages.vendorLocation(fb.fetch(0, 2128));
        BootImages.requireLinux(fb.fetch(config[0], (int) config[1]));
        appendLog("[Restore] Verified Linux in Slot B. Restoring its three boot images...");
        for (int i = 0; i < partitions.length; i++) flashOne(fb, partitions[i], images[i]);
        fb.sendCommand("set_active:b");
        if (autoReboot) fb.sendCommand("reboot");
        appendLog("[SUCCESS] Linux Slot B restored. Slot A and Android metadata were not rewritten.");
    }

    private void flashOne(FastbootTransport fb, String partition, ImageSource src) throws Exception {
        appendLog(String.format(Locale.US, "[Flash] Sending %s (%d KB from %s)...",
                partition, src.size / 1024, src.label));
        InputStream in = src.open();
        try {
            fb.flashPartition(partition, in, src.size);
        } finally {
            in.close();
        }
    }

    private static class ImageSource {
        final String label;
        final long size;
        final File file;
        final AssetManager assets;
        final String assetPath;
        final ContentResolver resolver;
        final Uri uri;

        ImageSource(File f) {
            this.label = f.getAbsolutePath();
            this.size = f.length();
            this.file = f;
            this.assets = null;
            this.assetPath = null;
            this.resolver = null;
            this.uri = null;
        }

        ImageSource(AssetManager am, String path, long size) {
            this.label = "assets/" + path;
            this.size = size;
            this.file = null;
            this.assets = am;
            this.assetPath = path;
            this.resolver = null;
            this.uri = null;
        }

        ImageSource(ContentResolver resolver, Uri uri, long size) {
            this.label = uri.toString();
            this.size = size;
            this.file = null;
            this.assets = null;
            this.assetPath = null;
            this.resolver = resolver;
            this.uri = uri;
        }

        InputStream open() throws IOException {
            if (uri != null) {
                InputStream stream = resolver.openInputStream(uri);
                if (stream == null) throw new IOException("Cannot read " + label);
                return new BufferedInputStream(stream, 262144);
            }
            if (file != null) {
                return new BufferedInputStream(new FileInputStream(file), 262144);
            }
            return new BufferedInputStream(assets.open(assetPath), 262144);
        }
    }

    private ImageSource resolveImage(String name) throws IOException {
        if (backupTree != null) {
            Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(backupTree, DocumentsContract.getTreeDocumentId(backupTree));
            String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID, DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_SIZE};
            try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
                if (cursor != null) while (cursor.moveToNext()) {
                    if (name.equals(cursor.getString(1)) && !cursor.isNull(2) && cursor.getLong(2) > 0) {
                        Uri document = DocumentsContract.buildDocumentUriUsingTree(backupTree, cursor.getString(0));
                        return new ImageSource(getContentResolver(), document, cursor.getLong(2));
                    }
                }
            }
            throw new IOException("Missing or empty " + name + " in selected backup folder");
        }
        File extDir = new File(getExternalFilesDir(null), "slotb-backup");
        File f2 = new File(extDir, name);
        if (f2.isFile() && f2.canRead() && f2.length() > 0) {
            return new ImageSource(f2);
        }
        String assetPath = "slotb-backup/" + name;
        try {
            AssetFileDescriptor afd = getAssets().openFd(assetPath);
            long len = afd.getLength();
            afd.close();
            if (len > 0) {
                return new ImageSource(getAssets(), assetPath, len);
            }
        } catch (IOException ignored) {
        }
        throw new IOException("Missing " + name + ". Choose a folder containing boot_b.img, init_boot_b.img, vendor_boot_b.img");
    }

    private UsbDevice findFastbootUsbDevice() {
        Map<String, UsbDevice> map = usbManager.getDeviceList();
        for (UsbDevice dev : map.values()) {
            for (int i = 0; i < dev.getInterfaceCount(); i++) {
                UsbInterface intf = dev.getInterface(i);
                if (intf.getInterfaceClass() == FASTBOOT_CLASS &&
                        intf.getInterfaceSubclass() == FASTBOOT_SUBCLASS &&
                        intf.getInterfaceProtocol() == FASTBOOT_PROTOCOL) {
                    return dev;
                }
            }
            if (dev.getVendorId() == GOOGLE_VID) {
                return dev;
            }
        }
        return null;
    }

    private FastbootTransport openUsbTransport() throws IOException {
        UsbDevice dev = findFastbootUsbDevice();
        if (dev == null) {
            throw new IOException("No Fastboot USB device connected");
        }
        UsbInterface targetIntf = null;
        UsbEndpoint epIn = null;
        UsbEndpoint epOut = null;
        for (int i = 0; i < dev.getInterfaceCount(); i++) {
            UsbInterface intf = dev.getInterface(i);
            if ((intf.getInterfaceClass() == FASTBOOT_CLASS &&
                    intf.getInterfaceSubclass() == FASTBOOT_SUBCLASS &&
                    intf.getInterfaceProtocol() == FASTBOOT_PROTOCOL) ||
                    dev.getVendorId() == GOOGLE_VID) {
                UsbEndpoint in = null, out = null;
                for (int e = 0; e < intf.getEndpointCount(); e++) {
                    UsbEndpoint ep = intf.getEndpoint(e);
                    if (ep.getType() == UsbConstants.USB_ENDPOINT_XFER_BULK) {
                        if (ep.getDirection() == UsbConstants.USB_DIR_IN) in = ep;
                        else out = ep;
                    }
                }
                if (in != null && out != null) {
                    targetIntf = intf;
                    epIn = in;
                    epOut = out;
                    break;
                }
            }
        }
        if (targetIntf == null) {
            throw new IOException("Fastboot bulk IN/OUT endpoints not found on USB device");
        }
        UsbDeviceConnection conn = usbManager.openDevice(dev);
        if (conn == null || !conn.claimInterface(targetIntf, true)) {
            throw new IOException("Failed to open/claim USB Fastboot interface");
        }
        return new UsbFastbootTransport(conn, targetIntf, epIn, epOut);
    }

    private FastbootTransport openTcpTransport(String hostPort) throws IOException {
        String host = hostPort;
        int port = 5554;
        if (host.startsWith("tcp:")) {
            host = host.substring(4);
        }
        int colon = host.indexOf(':');
        if (colon > 0) {
            port = Integer.parseInt(host.substring(colon + 1));
            host = host.substring(0, colon);
        }
        Socket sock = new Socket();
        sock.connect(new InetSocketAddress(host, port), 5000);
        sock.setSoTimeout(60000);
        return new TcpFastbootTransport(sock);
    }

    private interface FastbootTransport {
        String sendCommand(String cmd) throws IOException;
        String getVar(String name) throws IOException;
        void flashPartition(String partition, InputStream data, long size) throws IOException;
        byte[] fetch(long offset, int size) throws IOException;
        void close();
    }

    private class UsbFastbootTransport implements FastbootTransport {
        private final UsbDeviceConnection conn;
        private final UsbInterface intf;
        private final UsbEndpoint epIn;
        private final UsbEndpoint epOut;

        UsbFastbootTransport(UsbDeviceConnection conn, UsbInterface intf, UsbEndpoint epIn, UsbEndpoint epOut) {
            this.conn = conn;
            this.intf = intf;
            this.epIn = epIn;
            this.epOut = epOut;
        }

        @Override
        public String sendCommand(String cmd) throws IOException {
            byte[] outBytes = cmd.getBytes(StandardCharsets.UTF_8);
            int w = conn.bulkTransfer(epOut, outBytes, outBytes.length, 5000);
            if (w != outBytes.length) {
                throw new IOException("USB write failed for command: " + cmd);
            }
            return readResponse();
        }

        private String readResponse() throws IOException {
            byte[] buf = new byte[256];
            while (true) {
                int r = conn.bulkTransfer(epIn, buf, buf.length, 60000);
                if (r < 4) {
                    throw new IOException("USB read timeout or short packet (" + r + ")");
                }
                String s = new String(buf, 0, r, StandardCharsets.UTF_8);
                if (s.startsWith("INFO")) {
                    appendLog("  (bootloader) " + s.substring(4).trim());
                } else if (s.startsWith("OKAY")) {
                    return s.substring(4);
                } else if (s.startsWith("DATA")) {
                    return s;
                } else if (s.startsWith("FAIL")) {
                    throw new IOException("Fastboot FAIL: " + s.substring(4));
                }
            }
        }

        @Override
        public String getVar(String name) throws IOException {
            return sendCommand("getvar:" + name);
        }

        @Override
        public void flashPartition(String partition, InputStream data, long size) throws IOException {
            String dlCmd = String.format(Locale.US, "download:%08x", size);
            String resp = sendCommand(dlCmd);
            if (!resp.startsWith("DATA")) {
                throw new IOException("Expected DATA response for " + dlCmd + ", got: " + resp);
            }
            byte[] chunk = new byte[16384];
            long remaining = size;
            while (remaining > 0) {
                int toRead = (int) Math.min(chunk.length, remaining);
                int n = readFully(data, chunk, toRead);
                if (n <= 0) throw new IOException("Unexpected EOF reading image");
                int w = conn.bulkTransfer(epOut, chunk, n, 30000);
                if (w != n) {
                    throw new IOException("USB bulk data transfer failed (" + w + " != " + n + ")");
                }
                remaining -= n;
            }
            readResponse();
            sendCommand("flash:" + partition);
        }

        @Override
        public byte[] fetch(long offset, int size) throws IOException {
            String response = sendCommand(String.format(Locale.US, "fetch:vendor_boot_b:0x%08x:0x%08x", offset, size));
            if (!response.equals(String.format(Locale.US, "DATA%08x", size))) throw new IOException("Cannot verify Slot B: fetch unsupported or wrong size");
            byte[] result = new byte[size];
            int received = 0;
            while (received < size) {
                int n = conn.bulkTransfer(epIn, result, received, Math.min(16384, size - received), 60000);
                if (n <= 0) throw new IOException("Incomplete Fastboot fetch");
                received += n;
            }
            if (readResponse().startsWith("DATA")) throw new IOException("Unexpected fetch response");
            return result;
        }

        @Override
        public void close() {
            try {
                conn.releaseInterface(intf);
                conn.close();
            } catch (Exception ignored) {
            }
        }
    }

    private class TcpFastbootTransport implements FastbootTransport {
        private final Socket sock;
        private final DataInputStream in;
        private final DataOutputStream out;

        TcpFastbootTransport(Socket sock) throws IOException {
            this.sock = sock;
            this.in = new DataInputStream(sock.getInputStream());
            this.out = new DataOutputStream(sock.getOutputStream());
            out.write("FB01".getBytes(StandardCharsets.US_ASCII));
            out.flush();
            byte[] hs = new byte[4];
            in.readFully(hs);
            String magic = new String(hs, StandardCharsets.US_ASCII);
            if (!"FB01".equals(magic)) {
                throw new IOException("Invalid TCP Fastboot handshake: " + magic);
            }
        }

        private void writePacket(byte[] data, int len) throws IOException {
            out.writeLong(len);
            out.write(data, 0, len);
            out.flush();
        }

        private String readResponse() throws IOException {
            while (true) {
                long len = in.readLong();
                if (len < 4 || len > 65536) {
                    throw new IOException("Invalid TCP Fastboot response length: " + len);
                }
                byte[] buf = new byte[(int) len];
                in.readFully(buf);
                String s = new String(buf, StandardCharsets.UTF_8);
                if (s.startsWith("INFO")) {
                    appendLog("  (bootloader) " + s.substring(4).trim());
                } else if (s.startsWith("OKAY")) {
                    return s.substring(4);
                } else if (s.startsWith("DATA")) {
                    return s;
                } else if (s.startsWith("FAIL")) {
                    throw new IOException("Fastboot FAIL: " + s.substring(4));
                }
            }
        }

        @Override
        public String sendCommand(String cmd) throws IOException {
            byte[] b = cmd.getBytes(StandardCharsets.UTF_8);
            writePacket(b, b.length);
            return readResponse();
        }

        @Override
        public String getVar(String name) throws IOException {
            return sendCommand("getvar:" + name);
        }

        @Override
        public void flashPartition(String partition, InputStream data, long size) throws IOException {
            String dlCmd = String.format(Locale.US, "download:%08x", size);
            String resp = sendCommand(dlCmd);
            if (!resp.startsWith("DATA")) {
                throw new IOException("Expected DATA response for " + dlCmd + ", got: " + resp);
            }
            byte[] chunk = new byte[65536];
            long remaining = size;
            while (remaining > 0) {
                int toRead = (int) Math.min(chunk.length, remaining);
                int n = readFully(data, chunk, toRead);
                if (n <= 0) throw new IOException("Unexpected EOF reading image");
                writePacket(chunk, n);
                remaining -= n;
            }
            readResponse();
            sendCommand("flash:" + partition);
        }

        @Override
        public byte[] fetch(long offset, int size) throws IOException {
            String response = sendCommand(String.format(Locale.US, "fetch:vendor_boot_b:0x%08x:0x%08x", offset, size));
            if (!response.equals(String.format(Locale.US, "DATA%08x", size))) throw new IOException("Cannot verify Slot B: fetch unsupported or wrong size");
            byte[] result = new byte[size];
            int received = 0;
            while (received < size) {
                long packet = in.readLong();
                if (packet <= 0 || packet > size - received) throw new IOException("Invalid fetch packet size");
                in.readFully(result, received, (int) packet);
                received += (int) packet;
            }
            if (readResponse().startsWith("DATA")) throw new IOException("Unexpected fetch response");
            return result;
        }

        @Override
        public void close() {
            try {
                sock.close();
            } catch (Exception ignored) {
            }
        }
    }

    private static int readFully(InputStream in, byte[] buf, int len) throws IOException {
        int total = 0;
        while (total < len) {
            int n = in.read(buf, total, len - total);
            if (n < 0) break;
            total += n;
        }
        return total;
    }
}
