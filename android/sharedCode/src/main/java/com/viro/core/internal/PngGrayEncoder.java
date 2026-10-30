//
//  PngGrayEncoder.java
//  ViroCore
//
//  Minimal grayscale PNG encoder. android.graphics.Bitmap.compress() cannot
//  write a 16-bit gray PNG, which the recorded depth maps need to keep
//  millimetre precision beyond 255.
//
//  Copyright © 2026 ReactVision. All rights reserved.
//  MIT License — see LICENSE file.
//
package com.viro.core.internal;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.zip.CRC32;
import java.util.zip.Deflater;

public final class PngGrayEncoder {

    private static final byte[] SIGNATURE = {
        (byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'
    };

    private PngGrayEncoder() {}

    /**
     * Encodes a 16-bit grayscale PNG.
     *
     * @param samples      width * height unsigned 16-bit samples, row-major, tightly packed,
     *                     two bytes each in the order given by littleEndian.
     * @param littleEndian Whether samples holds the low byte of each sample first. PNG itself
     *                     stores them big-endian.
     */
    public static byte[] encodeGray16(byte[] samples, int width, int height, boolean littleEndian)
            throws IOException {
        checkSize(samples, width, height, 2);
        int rowBytes = width * 2;
        byte[] raw = new byte[(rowBytes + 1) * height];
        int src = 0, dst = 0;
        for (int row = 0; row < height; row++) {
            raw[dst++] = 0; // filter type: none
            for (int col = 0; col < width; col++) {
                if (littleEndian) {
                    raw[dst++] = samples[src + 1];
                    raw[dst++] = samples[src];
                } else {
                    raw[dst++] = samples[src];
                    raw[dst++] = samples[src + 1];
                }
                src += 2;
            }
        }
        return encode(raw, width, height, 16);
    }

    /**
     * Encodes an 8-bit grayscale PNG from width * height tightly packed samples.
     */
    public static byte[] encodeGray8(byte[] samples, int width, int height) throws IOException {
        checkSize(samples, width, height, 1);
        byte[] raw = new byte[(width + 1) * height];
        int src = 0, dst = 0;
        for (int row = 0; row < height; row++) {
            raw[dst++] = 0; // filter type: none
            System.arraycopy(samples, src, raw, dst, width);
            src += width;
            dst += width;
        }
        return encode(raw, width, height, 8);
    }

    private static void checkSize(byte[] samples, int width, int height, int bytesPerSample) {
        if (width <= 0 || height <= 0 || samples == null
                || samples.length != (long) width * height * bytesPerSample) {
            throw new IllegalArgumentException("Sample buffer does not match " + width + "x" + height);
        }
    }

    private static byte[] encode(byte[] raw, int width, int height, int bitDepth) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream(raw.length / 2 + 128);
        out.write(SIGNATURE);

        byte[] ihdr = new byte[13];
        putInt(ihdr, 0, width);
        putInt(ihdr, 4, height);
        ihdr[8] = (byte) bitDepth;
        ihdr[9] = 0;  // colour type: grayscale
        ihdr[10] = 0; // compression: deflate
        ihdr[11] = 0; // filter method: adaptive, per-row type byte
        ihdr[12] = 0; // interlace: none
        writeChunk(out, "IHDR", ihdr, 0, ihdr.length);

        Deflater deflater = new Deflater(Deflater.DEFAULT_COMPRESSION);
        try {
            deflater.setInput(raw);
            deflater.finish();
            byte[] buf = new byte[64 * 1024];
            ByteArrayOutputStream idat = new ByteArrayOutputStream(raw.length / 2 + 64);
            while (!deflater.finished()) {
                int n = deflater.deflate(buf);
                idat.write(buf, 0, n);
            }
            byte[] data = idat.toByteArray();
            writeChunk(out, "IDAT", data, 0, data.length);
        } finally {
            deflater.end();
        }

        writeChunk(out, "IEND", new byte[0], 0, 0);
        return out.toByteArray();
    }

    private static void writeChunk(ByteArrayOutputStream out, String type, byte[] data,
                                   int offset, int length) throws IOException {
        byte[] header = new byte[8];
        putInt(header, 0, length);
        for (int i = 0; i < 4; i++) {
            header[4 + i] = (byte) type.charAt(i);
        }
        out.write(header);
        out.write(data, offset, length);

        CRC32 crc = new CRC32();
        crc.update(header, 4, 4);
        crc.update(data, offset, length);
        byte[] crcBytes = new byte[4];
        putInt(crcBytes, 0, (int) crc.getValue());
        out.write(crcBytes);
    }

    private static void putInt(byte[] dst, int offset, int value) {
        dst[offset] = (byte) (value >>> 24);
        dst[offset + 1] = (byte) (value >>> 16);
        dst[offset + 2] = (byte) (value >>> 8);
        dst[offset + 3] = (byte) value;
    }
}
