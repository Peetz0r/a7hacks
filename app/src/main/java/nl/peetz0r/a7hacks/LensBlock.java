package nl.peetz0r.a7hacks;

import java.io.UnsupportedEncodingException;

/**
 * What goes into the camera's "manual lens EXIF info" block, as arguments for liba7hacks.so --lens-write:
 * "1 focalNum focalDen fNum fDen maxApNum maxApDen nameHex", or "0" to switch the block off.
 * The camera turns this into EXIF FocalLength, FocalLengthIn35mmFilm, LensModel, MaxApertureValue and
 * FNumber (always 0 here: the aperture ring of an adapted lens is unknown). It copies maxAp into MaxApertureValue
 * as it is, and that tag is an APEX value (2 log2 N), not an F-number: f/2.8 goes in as 297/100. Pure Java.
 */
public final class LensBlock {
    public static final String OFF = "0";
    /** Longest lens name the block takes (65 bytes including the terminating zero). */
    public static final int MAX_NAME = 64;
    /** Lens name used when A7HACKS.INI has no model for the focal length (what the camera writes without a lens). */
    public static final String NO_NAME = "----";

    private LensBlock() {}

    /** Arguments for this focal length (mm) and profile (may be null); the name is cut to maxNameBytes. */
    public static String args(int mm, LensProfiles.Profile p, int maxNameBytes) {
        StringBuilder sb = new StringBuilder("1 ");
        sb.append(mm * 10).append(" 10 0 1 ");
        if (p != null && p.aperture > 0) sb.append(apex(p.aperture));
        else sb.append("0 1");
        String name = p != null && p.model != null ? p.model : NO_NAME;
        String hex = hex(clip(name, Math.min(MAX_NAME, maxNameBytes)));
        if (hex.length() > 0) sb.append(' ').append(hex);
        return sb.toString();
    }

    /** An F-number as an APEX aperture value (2 log2 N) in hundredths, as cameras write MaxApertureValue:
     *  2.8 -> "297 100", 1.4 -> "97 100", 4 -> "400 100". Below f/1 it would be negative: 0 then. */
    static String apex(double n) {
        long v = Math.round(200 * Math.log(n) / Math.log(2));
        return Math.max(0, v) + " 100";
    }

    /** The UTF-8 bytes of s, at most max bytes, never cutting a character in half. */
    static byte[] clip(String s, int max) {
        byte[] b;
        try { b = s.getBytes("UTF-8"); } catch (UnsupportedEncodingException e) { b = s.getBytes(); }
        if (max < 0) max = 0;
        if (b.length <= max) return b;
        int n = max;
        while (n > 0 && (b[n] & 0xC0) == 0x80) n--;    // b[n] would be the first byte left out
        byte[] r = new byte[n];
        System.arraycopy(b, 0, r, 0, n);
        return r;
    }

    static String hex(byte[] b) {
        StringBuilder sb = new StringBuilder();
        for (byte x : b) sb.append(Character.forDigit((x >> 4) & 15, 16)).append(Character.forDigit(x & 15, 16));
        return sb.toString();
    }

    /** One line for the log. */
    public static String describe(int mm, LensProfiles.Profile p) {
        if (p == null || (p.model == null && p.aperture <= 0))
            return mm + " mm: not in A7HACKS.INI, writing the focal length only";
        return mm + " mm: " + (p.model != null ? p.model : NO_NAME)
            + (p.aperture > 0 ? " (f/" + trim(p.aperture) + ")" : "");
    }

    static String trim(double v) {
        String s = String.valueOf(Math.round(v * 100) / 100.0);
        return s.endsWith(".0") ? s.substring(0, s.length() - 2) : s;
    }
}
