// Ghidra post-script: export a complete static-analysis dossier for DKII.EXE.
// Run with analyzeHeadless -postScript GhidraExportScript.java
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.Program;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.util.task.ConsoleTaskMonitor;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

public class GhidraExportScript extends GhidraScript {
    private static String esc(String s) {
        if (s == null) return "";
        return s.replace("\\", "\\\\").replace("\"", "\\\"")
                .replace("\n", "\\n").replace("\r", "");
    }

    private static void write(Path p, String content) throws Exception {
        Files.createDirectories(p.getParent());
        Files.write(p, content.getBytes(StandardCharsets.UTF_8));
    }

    @Override
    public void run() throws Exception {
        String outDir = System.getenv("DK2_GHIDRA_OUT");
        if (outDir == null || outDir.length() == 0) outDir = "/home/kali/Desktop/rea/Dungeon Keeper 2/port/re/ghidra";
        Path root = Paths.get(outDir);
        Files.createDirectories(root.resolve("decomp"));
        Files.createDirectories(root.resolve("asm"));
        println("DK2 EXPORT: output = " + root);

        Program prog = currentProgram;
        println("DK2 EXPORT: program = " + prog.getName());

        StringBuilder fn = new StringBuilder("[\n");
        StringBuilder strings = new StringBuilder("[\n");
        StringBuilder blocks = new StringBuilder("[\n");
        StringBuilder symbols = new StringBuilder("[\n");

        MemoryBlock[] mem = prog.getMemory().getBlocks();
        for (int i = 0; i < mem.length; i++) {
            MemoryBlock b = mem[i];
            if (i > 0) blocks.append(",\n");
            blocks.append("{\"name\":\"").append(esc(b.getName())).append("\",")
                  .append("\"start\":\"").append(b.getStart().toString()).append("\",")
                  .append("\"end\":\"").append(b.getEnd().toString()).append("\",")
                  .append("\"read\":").append(b.isRead()).append(",\"write\":").append(b.isWrite())
                  .append(",\"execute\":").append(b.isExecute())
                  .append(",\"size\":").append(b.getSize()).append("}");
        }
        blocks.append("\n]");

        SymbolIterator sit = prog.getSymbolTable().getAllSymbols(true);
        int sc = 0;
        while (sit.hasNext() && sc < 200000) {
            Symbol s = sit.next();
            if (sc++ > 0) symbols.append(",\n");
            symbols.append("{\"name\":\"").append(esc(s.getName())).append("\",")
                   .append("\"address\":\"").append(s.getAddress().toString()).append("\",")
                   .append("\"type\":\"").append(esc(s.getSymbolType().toString())).append("\"}");
        }
        symbols.append("\n]");

        Listing listing = prog.getListing();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(prog);
        decomp.setSimplificationStyle("decompile");
        ConsoleTaskMonitor monitor = new ConsoleTaskMonitor();

        FunctionIterator functions = prog.getFunctionManager().getFunctions(true);
        int count = 0, decompiled = 0, failed = 0;
        List<String> heavy = new ArrayList<String>();
        while (functions.hasNext()) {
            Function f = functions.next();
            Address entry = f.getEntryPoint();
            String name = f.getName();
            String sig = "";
            try { sig = f.getSignature().getPrototypeString(); } catch (Exception e) { sig = name; }
            int size = (int) f.getBody().getNumAddresses();

            ReferenceManager rm = prog.getReferenceManager();
            int callers = 0;
            ReferenceIterator to = rm.getReferencesTo(entry);
            while (to.hasNext()) {
                Reference r = to.next();
                if (r.getReferenceType().isCall()) callers++;
            }

            if (count > 0) fn.append(",\n");
            fn.append("{\"address\":\"").append(entry.toString()).append("\",")
              .append("\"name\":\"").append(esc(name)).append("\",")
              .append("\"signature\":\"").append(esc(sig)).append("\",")
              .append("\"size\":").append(size).append(",")
              .append("\"callers\":").append(callers).append(",")
              .append("\"thunk\":").append(f.isThunk()).append(",")
              .append("\"external\":").append(f.isExternal()).append("}");

            StringBuilder asm = new StringBuilder();
            Instruction ins = listing.getInstructionAt(entry);
            int insCount = 0;
            while (ins != null && insCount < 20000 && f.getBody().contains(ins.getAddress())) {
                asm.append(ins.getAddress().toString()).append("  ")
                   .append(esc(ins.toString())).append("\n");
                ins = ins.getNext();
                insCount++;
            }
            try {
                write(root.resolve("asm").resolve(entry.toString().replace(":", "_") + ".asm"),
                      asm.toString());
            } catch (Exception e) { }

            try {
                DecompileResults res = decomp.decompileFunction(f, 90, monitor);
                if (res != null && res.decompileCompleted()) {
                    String code = res.getDecompiledFunction().getC();
                    write(root.resolve("decomp").resolve(entry.toString().replace(":", "_") + ".c"),
                          code);
                    decompiled++;
                    if (code.length() > 60000) heavy.add(entry + " " + name + " " + code.length());
                } else {
                    failed++;
                }
            } catch (Exception e) {
                failed++;
            }
            count++;
            if (count % 250 == 0) println("DK2 EXPORT: " + count + " functions, " + decompiled + " decompiled");
        }
        fn.append("\n]");
        write(root.resolve("functions.json"), fn.toString());
        write(root.resolve("symbols.json"), symbols.toString());
        write(root.resolve("memory.json"), blocks.toString());
        write(root.resolve("strings.json"), strings.toString());
        write(root.resolve("export_summary.txt"),
              "functions=" + count + "\ndecompiled=" + decompiled + "\nfailed=" + failed +
              "\nheavy=" + heavy.size() + "\n" + String.join("\n", heavy));
        println("DK2 EXPORT DONE: functions=" + count + " decompiled=" + decompiled + " failed=" + failed);
        decomp.dispose();
    }
}
