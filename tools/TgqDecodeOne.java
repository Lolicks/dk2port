import java.awt.image.BufferedImage;
import java.io.File;
import java.nio.file.Path;
import java.nio.file.Paths;
import javax.imageio.ImageIO;
import toniarts.openkeeper.video.tgq.EAAudioFrame;
import toniarts.openkeeper.video.tgq.EAAudioHeader;
import toniarts.openkeeper.video.tgq.TgqFile;
import toniarts.openkeeper.video.tgq.TgqFrame;

public final class TgqDecodeOne {
    public static void main(String[] args) throws Exception {
        if (args.length != 3) {
            throw new IllegalArgumentException("usage: TgqDecodeOne <movie.tgq> <outdir> <maxFrames>");
        }
        Path movie = Paths.get(args[0]);
        File outDir = new File(args[1]);
        if (!outDir.isDirectory()) {
            throw new IllegalArgumentException("output directory does not exist: " + args[1]);
        }
        int maxFrames = Integer.parseInt(args[2]);
        final int[] written = {0};
        final int[] seen = {0};
        try (TgqFile tgq = new TgqFile(movie) {
            @Override
            protected void addVideoFrame(TgqFrame frame) {
                seen[0]++;
                if (written[0] < maxFrames) {
                    try {
                        BufferedImage image = frame.getImage();
                        File out = new File(outDir, String.format("frame_%03d.png", frame.getFrameIndex()));
                        if (!ImageIO.write(image, "png", out)) {
                            throw new RuntimeException("no PNG writer");
                        }
                        written[0]++;
                        System.out.printf(
                            "frame=%d size=%dx%d png=%s bytes=%d lumaLineSize=%d cbLineSize=%d crLineSize=%d%n",
                            frame.getFrameIndex(), frame.getWidth(), frame.getHeight(),
                            out.getName(), out.length(),
                            frame.getLineSize(TgqFrame.YCBCR_PLANE_LUMA),
                            frame.getLineSize(TgqFrame.YCBCR_PLANE_CB),
                            frame.getLineSize(TgqFrame.YCBCR_PLANE_CR));
                    } catch (Exception e) {
                        throw new RuntimeException("failed to write frame " + frame.getFrameIndex(), e);
                    }
                }
            }
            @Override
            protected void addAudioFrame(EAAudioFrame frame) { }
            @Override
            protected void onAudioHeader(EAAudioHeader header) {
                System.out.printf("audioHeader rate=%d bits=%d channels=%d compression=%s%n",
                    header.getSampleRate(), header.getBitsPerSample(),
                    header.getNumberOfChannels(), header.getCompression());
            }
        }) {
            while (tgq.readFrame() && written[0] < maxFrames) {
                // keep reading until the requested number of frames is decoded
            }
        }
        System.out.printf("movie=%s decoded=%d videoFramesSeen=%d%n", movie, written[0], seen[0]);
    }
}
