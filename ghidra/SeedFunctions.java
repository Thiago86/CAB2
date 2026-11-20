// Pre-script: cria funcoes nos enderecos de out/functions.csv antes da auto-analise.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import java.io.*;

public class SeedFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) { println("uso: SeedFunctions.java <functions.csv>"); return; }
        int made = 0, total = 0;
        try (BufferedReader r = new BufferedReader(new FileReader(args[0]))) {
            String line = r.readLine(); // cabecalho
            while ((line = r.readLine()) != null) {
                String a = line.split(",")[0].trim();
                if (!a.startsWith("0x")) continue;
                total++;
                Address addr = toAddr(Long.parseLong(a.substring(2), 16));
                if (!currentProgram.getMemory().contains(addr)) continue;
                disassemble(addr);
                if (getFunctionAt(addr) == null && createFunction(addr, null) != null) made++;
            }
        }
        println("SeedFunctions: " + made + " funcoes criadas de " + total);
    }
}
