import java.nio.file.Paths;
import toniarts.openkeeper.tools.convert.textures.enginetextures.EngineTexturesFile;

public final class TextureMain {
    public static void main(String[] args) {
        EngineTexturesFile f = new EngineTexturesFile(Paths.get(args[0]));
        System.out.println("entries=" + f.getFileCount());
        f.extractFileData(args[1]);
        System.out.println("extracted to " + args[1]);
    }
}
