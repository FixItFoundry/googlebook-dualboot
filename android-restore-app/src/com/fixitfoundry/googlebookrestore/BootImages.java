package com.fixitfoundry.googlebookrestore;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

final class BootImages {
    static byte[] read(InputStream in, int size) throws IOException {
        byte[] data = new byte[size];
        int offset = 0;
        while (offset < size) {
            int n = in.read(data, offset, size - offset);
            if (n <= 0) throw new IOException("Truncated image");
            offset += n;
        }
        return data;
    }

    static long u32(byte[] data, int at) {
        return (data[at] & 255L) | ((data[at + 1] & 255L) << 8)
                | ((data[at + 2] & 255L) << 16) | ((data[at + 3] & 255L) << 24);
    }

    static long align(long size, long page) {
        return (size + page - 1) / page * page;
    }

    static long[] vendorLocation(byte[] h) throws IOException {
        if (h.length != 2128 || !new String(h, 0, 8, StandardCharsets.US_ASCII).equals("VNDRBOOT")
                || u32(h, 8) != 4 || u32(h, 2096) != 2128) {
            throw new IOException("Unsupported vendor boot header");
        }
        long page = u32(h, 12), table = u32(h, 2112), size = u32(h, 2124);
        if ((page != 2048 && page != 4096 && page != 8192 && page != 16384)
                || u32(h, 2120) != 108 || table != u32(h, 2116) * 108
                || size == 0 || size > 65536) {
            throw new IOException("Invalid vendor boot layout");
        }
        long offset = align(2128, page) + align(u32(h, 24), page)
                + align(u32(h, 2100), page) + align(table, page);
        if (offset + size > 32L * 1024 * 1024) throw new IOException("Bootconfig exceeds partition");
        return new long[]{offset, size};
    }

    static String requireLinux(byte[] config) throws IOException {
        String text = new String(config, StandardCharsets.US_ASCII);
        if (!java.util.regex.Pattern.compile("(?m)^androidboot\\.(googleboot|fedomakase)=1\\r?$")
                .matcher(text).find()) {
            throw new IOException("Backup vendor_boot_b.img is missing Linux bootconfig marker.");
        }
        try {
            byte[] digest = java.security.MessageDigest.getInstance("SHA-256").digest(config);
            StringBuilder sb = new StringBuilder(64);
            for (byte b : digest) sb.append(String.format(java.util.Locale.US, "%02x", b & 0xff));
            return sb.toString();
        } catch (Exception e) {
            throw new IOException(e);
        }
    }

    static String validate(String partition, InputStream in, long size) throws IOException {
        long limit = partition.equals("boot_b") ? 64L << 20
                : partition.equals("init_boot_b") ? 8L << 20 : 32L << 20;
        if (size < 2128 || size > limit) throw new IOException("Invalid image size: " + partition);
        if (partition.equals("vendor_boot_b")) {
            long[] location = vendorLocation(read(in, 2128));
            if (location[0] + location[1] > size) throw new IOException("Truncated bootconfig");
            long remaining = location[0] - 2128;
            while (remaining > 0) {
                long skipped = in.skip(remaining);
                if (skipped == 0) { if (in.read() < 0) throw new IOException("Truncated image"); skipped = 1; }
                remaining -= skipped;
            }
            String sha = requireLinux(read(in, (int) location[1]));
            return location[0] + ":" + location[1] + ":" + sha;
        } else {
            byte[] h = read(in, 1584);
            long kernel = u32(h, 8), ramdisk = u32(h, 12);
            if (!new String(h, 0, 8, StandardCharsets.US_ASCII).equals("ANDROID!")
                    || u32(h, 20) != 1584 || u32(h, 40) != 4
                    || (partition.equals("boot_b") ? kernel == 0 : kernel != 0 || ramdisk == 0)
                    || 4096 + align(kernel, 4096) + align(ramdisk, 4096) > size) {
                throw new IOException("Invalid Android v4 image: " + partition);
            }
            return "";
        }
    }
}
