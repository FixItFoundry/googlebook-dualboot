package com.fixitfoundry.googlebookrestore;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.Arrays;

public class BootImagesTest {
    public static void main(String[] args) throws Exception {
        byte[] vendor = Files.readAllBytes(Paths.get(args[0], "vendor_boot_b.img"));
        long[] location = BootImages.vendorLocation(Arrays.copyOf(vendor, 2128));
        if (location[0] <= 65536) throw new AssertionError("Fixture must exercise late bootconfig");
        for (String part : new String[]{"boot_b", "init_boot_b", "vendor_boot_b"}) {
            byte[] bytes = Files.readAllBytes(Paths.get(args[0], part + ".img"));
            BootImages.validate(part, new ByteArrayInputStream(bytes), bytes.length);
        }
        byte[] bad = vendor.clone();
        Arrays.fill(bad, (int) location[0], (int) (location[0] + location[1]), (byte) 0);
        mustReject(bad);
        mustReject(Arrays.copyOf(vendor, 65536));
        bad = vendor.clone();
        bad[12] = 1;
        mustReject(bad);
        System.out.println("PASS: Android image validator accepts packed images and rejects missing markers, truncation and invalid layouts");
    }

    private static void mustReject(byte[] bytes) throws Exception {
        try {
            BootImages.validate("vendor_boot_b", new ByteArrayInputStream(bytes), bytes.length);
        } catch (IOException expected) {
            return;
        }
        throw new AssertionError("Unsafe image accepted");
    }
}
