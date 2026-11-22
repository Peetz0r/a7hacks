package nl.peetz0r.a7hacks;

/**
 * Keeps the camera's manual-lens EXIF block in line with the SteadyShot setting: writes it when the setting
 * changes, checks it every few seconds (the camera may reset it), and switches it off when the setting can't
 * be read. Call tick() about once a second. Pure Java (no Android classes) so it can be tested on a PC.
 */
public final class LensWriter {
    /** The SteadyShot focal length in mm, or -1 if it can't be read right now. */
    public interface Setting { int read(); }
    /** The lens profile for a focal length, or null. */
    public interface Profiles { LensProfiles.Profile get(int mm); }
    /** Runs liba7hacks.so --lens-write with these arguments and returns its answer line. */
    public interface Tool {
        String run(String args) throws Exception;
        /** How many bytes of lens name fit on the tool's command line. */
        int nameBudget();
    }
    public interface Log { void log(String line); }

    static final long OFF_AFTER_MS = 5000;    // setting unreadable this long: switch the block off
    static final long RECHECK_MS = 10000;     // compare the block with what it should be this often
    static final long RETRY_MS = 10000;       // after an error

    private final Setting setting;
    private final Profiles profiles;
    private final Tool tool;
    private final Log log;

    private String applied = null;            // arguments the camera's block is known to match
    private String lastError = null;
    private long lastRun = 0, lastGood = 0;
    private boolean started = false, stopped = false;

    public LensWriter(Setting setting, Profiles profiles, Tool tool, Log log) {
        this.setting = setting;
        this.profiles = profiles;
        this.tool = tool;
        this.log = log;
    }

    /** True once the tool refused to touch this camera (wrong firmware); only a restart tries again. */
    public boolean isStopped() { return stopped; }

    public void tick(long now) {
        if (stopped) return;
        if (!started) { started = true; lastGood = now; lastRun = now - RECHECK_MS; }
        int mm = setting.read();
        LensProfiles.Profile p = null;
        String want;
        if (mm > 0) {
            lastGood = now;
            p = profiles.get(mm);
            want = LensBlock.args(mm, p, tool.nameBudget());
        } else if (now - lastGood >= OFF_AFTER_MS) {
            want = LensBlock.OFF;
        } else {
            return;                            // a short gap in the samples: leave the block as it is
        }
        boolean changed = !want.equals(applied);
        if (!changed && now - lastRun < RECHECK_MS) return;
        if (lastError != null && now - lastRun < RETRY_MS) return;
        lastRun = now;

        String ans;
        try {
            ans = tool.run(want);
        } catch (Exception e) {
            ans = "error " + e;
        }
        if (ans == null) ans = "error no answer";
        String word = ans.indexOf(' ') > 0 ? ans.substring(0, ans.indexOf(' ')) : ans;
        if (word.equals("written") || word.equals("same") || word.equals("disabled") || word.equals("off")) {
            if (changed) {
                log.log(want.equals(LensBlock.OFF)
                    ? "SteadyShot setting can't be read: lens info off"
                    : LensBlock.describe(mm, p));
            } else if (word.equals("written") || word.equals("disabled")) {
                log.log("the camera had changed the lens info, set it again");
            }
            applied = want;
            lastError = null;
        } else if (word.equals("badsig")) {
            log.log("this is not the camera firmware α7-hacks was made for (ILCE-7M2 version 4.01): not writing anything");
            stopped = true;
        } else if (word.equals("badblk")) {
            log.log("the camera's lens info memory doesn't look as expected, not writing anything [" + ans + "]");
            stopped = true;
        } else {
            if (!ans.equals(lastError)) log.log("could not set the lens info: " + ans);
            lastError = ans;
            applied = null;
        }
    }
}
