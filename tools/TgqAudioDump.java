import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.file.Paths;

import toniarts.openkeeper.video.tgq.EAAudioFrame;
import toniarts.openkeeper.video.tgq.EAAudioHeader;
import toniarts.openkeeper.video.tgq.TgqFile;
import toniarts.openkeeper.video.tgq.TgqFrame;

/** Dumps the PCM produced by the UNMODIFIED OpenKeeper EAAudioFrame decoder. */
public final class TgqAudioDump {
    public static void main(String[] args) throws Exception {
        if (args.length != 2) {
            throw new IllegalArgumentException("usage: TgqAudioDump <movie.tgq> <out.raw>");
        }

        final int[] blocks = {0};
        final int[] frames = {0};
        try (FileOutputStream out = new FileOutputStream(args[1])) {
            try (TgqFile tgq = new TgqFile(Paths.get(args[0])) {
                @Override
                protected void addVideoFrame(TgqFrame frame) { }

                @Override
                protected void addAudioFrame(EAAudioFrame frame) {
                    try {
                        ByteBuffer pcm = frame.getPcm();
                        byte[] b = new byte[pcm.remaining()];
                        pcm.get(b);
                        out.write(b);
                        blocks[0]++;
                        frames[0] += b.length / 4;
                    } catch (Exception e) {
                        throw new RuntimeException("audio frame write failed", e);
                    }
                }

                @Override
                protected void onAudioHeader(EAAudioHeader header) {
                    System.out.printf("audioHeader rate=%d bits=%d channels=%d compression=%s%n",
                        header.getSampleRate(), header.getBitsPerSample(),
                        header.getNumberOfChannels(), header.getCompression());
                }
            }) {
                while (tgq.readFrame()) {
                    // consume the whole stream
                }
            }
        }
        System.out.printf("movie=%s blocks=%d frames=%d bytes=%d%n",
            args[0], blocks[0], frames[0], frames[0] * 4);
    }
}
