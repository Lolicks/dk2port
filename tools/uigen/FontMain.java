import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import javax.imageio.ImageIO;
import toniarts.openkeeper.tools.convert.FontCreator;
import toniarts.openkeeper.tools.convert.bf4.Bf4File;

public final class FontMain {
    public static void main(String[] args) throws Exception {
        Path src = Paths.get(args[0]);
        Path dst = Paths.get(args[1]);
        Files.createDirectories(dst);
        try (DirectoryStream<Path> ds = Files.newDirectoryStream(src, "FONT_*.bf4")) {
            for (Path p : ds) {
                String name = p.getFileName().toString();
                var m = java.util.regex.Pattern.compile("FONT_([A-Za-z]+)(\\d+)\\.bf4").matcher(name);
                if (!m.find()) continue;
                int size = Integer.parseInt(m.group(2));
                String base = Character.toUpperCase(m.group(1).charAt(0)) + m.group(1).substring(1).toLowerCase() + size;
                FontCreator fc = new FontCreator(new Bf4File(p), size, base + ".png");
                for (var img : fc.getFontImages()) {
                    ImageIO.write(img.fontImage(), "png", dst.resolve(img.fileName()).toFile());
                }
                Files.writeString(dst.resolve(base + ".fnt"), fc.getDescription(), StandardCharsets.UTF_8);
                System.out.println("FONT " + name + " -> " + base);
            }
        }
    }
}
