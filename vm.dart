import 'dart:io';
import 'dart:math';

// --- Data Structures for the VM ---

class SymbolTableEntry {
  final String name;
  String type; // "int", "bool", "string", "double"
  num value; // Stores int/bool/double value (use num for flexibility)
  String? sValue; // Stores string value
  bool active;

  SymbolTableEntry(this.name, this.type, this.value, this.sValue, this.active);
}

class Instruction {
  final int opcode;
  final String opName;
  String arg1 = '';
  String arg2 = '';
  String dest = ''; // Destination variable or label

  Instruction(this.opcode, this.opName);
}

class LabelMapEntry {
  final String name;
  final int instrIndex; // Index into instructions array

  LabelMapEntry(this.name, this.instrIndex);
}

class FunctionMapEntry {
  final String name;
  final int instrIndex; // Instruction index of the [0x01] entry
  final String params; // Parameter declaration string (e.g., "int x, int y")

  FunctionMapEntry(this.name, this.instrIndex, this.params);
}

// --- Global state (No limits enforced) ---
List<Instruction> instructions = [];
List<SymbolTableEntry> symbolTable = [];
List<LabelMapEntry> labelMap = [];
List<FunctionMapEntry> functionMap = [];

List<int> callStack = []; // Call stack for return addresses
int mainEntryPoint = -1;

// --- Utility Functions ---

String trim(String s) => s.trim();

// Replaces only the '\n' escape sequence in a string
String unescapeNewline(String s) {
  return s.replaceAll('\\n', '\n');
}

// Check if a string is a variable name (starts with a letter and not a quoted string)
bool isVariable(String s) {
  if (s.isEmpty || s.startsWith('"')) return false;
  return s.codeUnitAt(0) >= 65 && s.codeUnitAt(0) <= 90 || // A-Z
         s.codeUnitAt(0) >= 97 && s.codeUnitAt(0) <= 122; // a-z
}

// Find a variable by name in the symbol table
SymbolTableEntry? getSymbol(String name) {
  try {
    return symbolTable.firstWhere((s) => s.active && s.name == name);
  } catch (e) {
    return null;
  }
}

// Get the numerical value of an operand (either literal or variable)
num getNumericValue(String operand) { 
  if (isVariable(operand)) {
    final s = getSymbol(operand);
    if (s != null && (s.type == 'int' || s.type == 'bool' || s.type == 'double')) {
      return s.value;
    }
    stderr.writeln("VM Error: Undefined or non-numeric variable '$operand'.");
    exit(1);
  }
  // Assume it's a numeric literal: try double first, then int
  return double.tryParse(operand) ?? int.tryParse(operand) ?? 0;
}

// Get the string value of an operand (either literal or variable)
String getStringValue(String operand) {
  if (operand.startsWith('"') && operand.endsWith('"')) {
    // String literal: remove quotes
    return operand.substring(1, operand.length - 1);
  }

  if (isVariable(operand)) {
    final s = getSymbol(operand);
    if (s != null && s.type == 'string' && s.sValue != null) {
      return s.sValue!;
    }
  }

  // Not a string variable or literal
  return '';
}

// Set the value of a destination variable
void setSymbolValue(String name, String type, num val, String? sVal) {
  SymbolTableEntry? s = getSymbol(name);

  // If symbol doesn't exist, create it
  if (s == null) {
    s = SymbolTableEntry(name, type, val, sVal, true);
    symbolTable.add(s);
  } else {
    // Update existing symbol
    s.type = type;
    s.value = val;
    s.sValue = sVal;
  }
}

// Find instruction index for a label
int findLabel(String name) {
  try {
    return labelMap.firstWhere((l) => l.name == name).instrIndex;
  } catch (e) {
    stderr.writeln("VM Error: Label '$name' not found.");
    return -1;
  }
}

// Find function entry by name
FunctionMapEntry? findFunction(String name) {
  try {
    return functionMap.firstWhere((f) => f.name == name);
  } catch (e) {
    return null;
  }
}

// Splits a comma-separated string of tokens, respecting quoted strings.
List<String> splitCommas(String s) {
  final out = <String>[];
  int start = 0;
  bool inStr = false;

  for (int i = 0; i < s.length; i++) {
    if (s[i] == '"') {
      inStr = !inStr;
    } else if (s[i] == ',' && !inStr) {
      out.add(trim(s.substring(start, i)));
      start = i + 1;
    }
  }

  // Add the last token
  if (start < s.length) {
    out.add(trim(s.substring(start)));
  }
  return out;
}

// --- Bytecode Loading ---

Future<void> loadBytecode(String filepath) async {
  final file = File(filepath);
  if (!await file.exists()) {
    stderr.writeln('Error: Bytecode file not found: $filepath');
    exit(1);
  }

  final lines = await file.readAsLines();

  for (final line in lines) {
    final trimmedLine = trim(line);
    if (trimmedLine.isEmpty || trimmedLine.startsWith('#')) continue;

    final match = RegExp(r'^\[0x([0-9a-fA-F]+)\]\s*(\w+)\s*(.*)$').firstMatch(trimmedLine);
    if (match == null) continue;

    final opcode = int.parse(match.group(1)!, radix: 16);
    final opName = match.group(2)!;
    final args = trim(match.group(3)!);

    final instr = Instruction(opcode, opName);

    switch (opcode) {
      case 0x01: // entry <type> <name>(<params>)
        final funcMatch = RegExp(r'^(\w+)\s+(\w+)\((.*?)\)$').firstMatch(args);
        if (funcMatch != null) {
          final name = funcMatch.group(2)!;
          final params = funcMatch.group(3)!;
          functionMap.add(FunctionMapEntry(name, instructions.length, params));
          if (name == 'main') {
            mainEntryPoint = instructions.length;
          }
        }
        break;
      case 0x03: // stdout <value>
      case 0x04: // stderr <value>
      case 0x05: // read <var>
      case 0x06: // return_code <var>
      case 0x08: // call <name>(<params>)
        instr.arg1 = args;
        break;
      case 0x07: { // store <type> <var> <value>
        final parts = args.split(RegExp(r'\s+'));
        if (parts.length >= 2) {
          instr.arg1 = parts[0]; // type
          instr.arg2 = parts[1]; // var
          // The rest is the value, including potential strings
          instr.dest = args.substring(args.indexOf(parts[1]) + parts[1].length).trim();
        }
        break;
      }
      case 0x13: // jz <cond_var> <label>
        final parts = args.split(RegExp(r'\s+'));
        if (parts.length >= 2) {
          instr.arg1 = parts[0]; // cond_var
          instr.dest = parts[1]; // label
        }
        break;
      case 0x14: // jmp <label>
        instr.dest = args;
        break;
      case 0x15: { // label <name>
        labelMap.add(LabelMapEntry(args, instructions.length));
        break;
      }
      // Binary Operators (0x09 - 0x12) <op1> <op2> <dest>
      case 0x09:
      case 0x0A:
      case 0x0B:
      case 0x0C:
      case 0x0D:
      case 0x0E:
      case 0x0F:
      case 0x10:
      case 0x11:
      case 0x12:
        final parts = args.split(RegExp(r'\s+'));
        if (parts.length >= 3) {
          instr.arg1 = parts[0];
          instr.arg2 = parts[1];
          instr.dest = parts[2];
        }
        break;
    }
    instructions.add(instr);
  }
}

// --- VM Execution ---

void executeVm() {
  if (mainEntryPoint == -1) {
    stderr.writeln("VM Error: Program does not contain an 'int main()' entry point.");
    return;
  }

  int pc = mainEntryPoint + 1; // Program Counter starts after 'entry'

  // Execution loop
  while (pc < instructions.length) {
    final instr = instructions[pc];

    // Skip 'label' instructions, they are only targets
    if (instr.opcode == 0x15) {
      pc++;
      continue;
    }

    num op1Val, op2Val;
    num result;
    String resultType;

    switch (instr.opcode) {
      case 0x02: // end
        return;

      case 0x03: { // stdout <value>
        String output;
        if (instr.arg1.startsWith('"')) {
          output = getStringValue(instr.arg1);
        } else if (isVariable(instr.arg1)) {
          final s = getSymbol(instr.arg1);
          if (s != null) {
            if (s.type == 'string' && s.sValue != null) {
              output = s.sValue!;
            } else if (s.type == 'int' || s.type == 'bool' || s.type == 'double') {
              // Print double as string, but only show integer part if it's a whole number
              if (s.type == 'double' && s.value == s.value.toInt()) {
                  output = s.value.toInt().toString();
              } else {
                  output = s.value.toString();
              }
            } else {
              output = '<unsupported type>';
            }
          } else {
            stderr.writeln("VM Error: Cannot print undefined variable '${instr.arg1}'.");
            pc++;
            continue;
          }
        } else {
          // Numeric literal: print double as string
          output = instr.arg1;
        }
        stdout.write(unescapeNewline(output));
        break;
      }

      case 0x04: { // stderr <value>
        String output;
        if (instr.arg1.startsWith('"')) {
          output = getStringValue(instr.arg1);
        } else {
          output = instr.arg1;
        }
        stderr.write(unescapeNewline(output));
        break;
      }

      case 0x05: { // read <var>
        final input = stdin.readLineSync();
        if (input != null) {
          final numVal = double.tryParse(input.trim());

          if (numVal != null) {
            if (numVal == numVal.toInt()) {
                // Integer input
                setSymbolValue(instr.arg1, 'int', numVal.toInt(), null);
            } else {
                // Double input
                setSymbolValue(instr.arg1, 'double', numVal, null);
            }
          } else {
            // String input
            setSymbolValue(instr.arg1, 'string', 0, input);
          }
        } else {
          stderr.writeln('VM Error: Failed to read input.');
          exit(1);
        }
        break;
      }

      case 0x06: { // return_code <var>
        if (callStack.isNotEmpty) {
          // Function return: Pop return address and jump
          pc = callStack.removeLast();
          continue; // Skip pc++ below
        } else {
          // Return from main
          return;
        }
      }

      case 0x07: { // store <type> <var> <value>
        final type = instr.arg1;
        final varName = instr.arg2;
        final value = instr.dest;

        if (value.startsWith('"')) {
          setSymbolValue(varName, 'string', 0, getStringValue(value));
        } else {
          // Use getNumericValue to correctly parse int/double literal
          num val = getNumericValue(value);
          setSymbolValue(varName, type, val, null);
        }
        break;
      }

      case 0x08: { // call <name>(<params>)
        final callSignature = instr.arg1;
        final nameMatch = RegExp(r'^(\w+)\((.*?)\)$').firstMatch(callSignature);

        if (nameMatch == null) {
          stderr.writeln('VM Error: Malformed call signature: $callSignature');
          exit(1);
        }

        final funcName = nameMatch.group(1)!;
        final argValuesStr = nameMatch.group(2)!;
        
        final funcEntry = findFunction(funcName);
        if (funcEntry == null) {
          stderr.writeln("VM Error: Function '$funcName' not found.");
          exit(1);
        }

        // 1. Parse arguments passed in the call and parameters declared in the function
        final argValues = splitCommas(argValuesStr);
        final paramTokens = splitCommas(funcEntry.params);

        if (argValues.length != paramTokens.length) {
          stderr.writeln("VM Error: Function '$funcName' called with ${argValues.length} arguments, expected ${paramTokens.length}.");
          exit(1);
        }

        // 2. Parameter assignment (pass by value)
        for (int i = 0; i < paramTokens.length; i++) {
          final paramMatch = RegExp(r'^(\w+)\s+(\w+)$').firstMatch(paramTokens[i]);
          if (paramMatch == null) {
            stderr.writeln("VM Error: Malformed parameter declaration in function '$funcName'.");
            exit(1);
          }
          final type = paramMatch.group(1)!;
          final paramName = paramMatch.group(2)!;
          final argValToken = argValues[i];

          if (type == 'string') {
            setSymbolValue(paramName, type, 0, getStringValue(argValToken));
          } else {
            // Use getNumericValue for pass-by-value of numeric types
            setSymbolValue(paramName, type, getNumericValue(argValToken), null);
          }
        }

        // 3. Save return address and jump
        callStack.add(pc + 1);
        pc = funcEntry.instrIndex + 1; // Jump after the 'entry' instruction
        continue; // Skip pc++
      }

      // Binary Arithmetic Operations (all use getNumericValue and num arithmetic)
      case 0x09: // ADD
        op1Val = getNumericValue(instr.arg1); op2Val = getNumericValue(instr.arg2); 
        result = op1Val + op2Val; 
        // Result is 'double' if either operand was double, otherwise 'int'
        resultType = (op1Val is double || op2Val is double || result != result.toInt()) ? 'double' : 'int'; 
        setSymbolValue(instr.dest, resultType, result, null); 
        break; 
        
      case 0x0A: // SUB
        op1Val = getNumericValue(instr.arg1); op2Val = getNumericValue(instr.arg2); 
        result = op1Val - op2Val; 
        resultType = (op1Val is double || op2Val is double || result != result.toInt()) ? 'double' : 'int'; 
        setSymbolValue(instr.dest, resultType, result, null); 
        break; 

      case 0x0B: // MUL
        op1Val = getNumericValue(instr.arg1); op2Val = getNumericValue(instr.arg2); 
        result = op1Val * op2Val; 
        resultType = (op1Val is double || op2Val is double || result != result.toInt()) ? 'double' : 'int'; 
        setSymbolValue(instr.dest, resultType, result, null); 
        break; 
        
      case 0x0C: // DIV (Floating-point division)
        op1Val = getNumericValue(instr.arg1); op2Val = getNumericValue(instr.arg2); 
        if (op2Val == 0) { stderr.writeln('VM Error: Division by zero.'); exit(1); }
        setSymbolValue(instr.dest, 'double', op1Val / op2Val, null); // Result is always 'double'
        break; 
        
      case 0x0D: // MOD (Integer remainder)
        op1Val = getNumericValue(instr.arg1); op2Val = getNumericValue(instr.arg2); 
        if (op2Val == 0) { stderr.writeln('VM Error: Division by zero (modulo).'); exit(1); }
        // Modulo is integer-based in most languages, so we explicitly cast to int for operation
        setSymbolValue(instr.dest, 'int', op1Val.toInt() % op2Val.toInt(), null); 
        break; 
        
      case 0x0E: // POW
        op1Val = getNumericValue(instr.arg1); op2Val = getNumericValue(instr.arg2); 
        result = pow(op1Val, op2Val).toDouble(); // pow result is always double in Dart
        setSymbolValue(instr.dest, 'double', result, null); 
        break; 

      // Comparison Operations (Result is 1 or 0)
      case 0x0F: op1Val = getNumericValue(instr.arg1); op2Val = getNumericValue(instr.arg2); setSymbolValue(instr.dest, 'bool', (op1Val > op2Val) ? 1 : 0, null); break; // GT
      case 0x10: op1Val = getNumericValue(instr.arg1); op2Val = getNumericValue(instr.arg2); setSymbolValue(instr.dest, 'bool', (op1Val < op2Val) ? 1 : 0, null); break; // LT
      case 0x11: op1Val = getNumericValue(instr.arg1); op2Val = getNumericValue(instr.arg2); setSymbolValue(instr.dest, 'bool', (op1Val == op2Val) ? 1 : 0, null); break; // EQ
      case 0x12: op1Val = getNumericValue(instr.arg1); op2Val = getNumericValue(instr.arg2); setSymbolValue(instr.dest, 'bool', (op1Val != op2Val) ? 1 : 0, null); break; // NE

      // Control Flow Jumps
      case 0x13: { // jz <cond_var> <label> (Jump if Zero/False)
        // Comparison vars are always int/bool (1 or 0)
        final condition = getSymbol(instr.arg1)?.value ?? 0;
        if (condition == 0) {
          final targetPc = findLabel(instr.dest);
          if (targetPc != -1) pc = targetPc;
          continue; // Skip pc++ below
        }
        break;
      }
      case 0x14: { // jmp <label> (Unconditional Jump)
        final targetPc = findLabel(instr.dest);
        if (targetPc != -1) pc = targetPc;
        continue; // Skip pc++ below
      }

      case 0x01: // entry: Already handled by initialization.
        break;

      default:
        stderr.writeln('VM Warning: Unhandled opcode 0x${instr.opcode.toRadixString(16).toUpperCase()} at instruction $pc.');
    }

    pc++; // Advance Program Counter
  }
}

// Main VM execution logic
Future<void> main(List<String> args) async {
  if (args.length < 1) {
    stderr.writeln('Usage: dart run vm.dart program.fluxb');
    exit(1);
  }

  await loadBytecode(args[0]);
  executeVm();
}
