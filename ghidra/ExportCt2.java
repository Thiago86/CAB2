// Post-script: exporta lista de funcoes e pseudocodigo C do decompilador.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.Function;
import java.io.*;

public class ExportCt2 extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args.length > 0 ? args[0] : ".");
        out.mkdirs();
        DecompInterface dec = new DecompInterface();
        dec.openProgram(currentProgram);
        int ok = 0, fail = 0;
        try (PrintWriter csv = new PrintWriter(new File(out, "ghidra_functions.csv"), "UTF-8");
             PrintWriter c = new PrintWriter(new File(out, "ghidra_decomp.c"), "UTF-8")) {
            csv.println("address,name,size_bytes,callers,callees,decompiled");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                DecompileResults r = dec.decompileFunction(f, 60, monitor);
                boolean done = r != null && r.decompileCompleted();
                if (done) { ok++; c.println("// ==== " + f.getEntryPoint() + " " + f.getName());
                            c.println(r.getDecompiledFunction().getC()); }
                else fail++;
                csv.println("0x" + f.getEntryPoint() + "," + f.getName() + "," + f.getBody().getNumAddresses() + ","
                        + f.getCallingFunctions(monitor).size() + "," + f.getCalledFunctions(monitor).size() + "," + done);
            }
        } finally { dec.dispose(); }
        println("ExportCt2: " + ok + " decompiladas, " + fail + " falharam -> " + out);
    }
}
