import 'dart:io';

// --- Global State for Control Flow Tracking ---

int ifCounter = 0;
List<int> ifStack = [];

int whileCounter = 0;
List<int> whileStack = [];

int forCounter = 0;
List<int> forStack = [];

// --- Utility Functions ---

String trim(String s) {
  return s.trim();
}

// Helper to split comma-separated args while respecting quoted strings
void splitCommas(String s, List<String> out, {int maxCount = 32}) {
  int count = 0;
  int start = 0;
  bool inStr = false;

  for (int i = 0; i < s.length; i++) {
    if (s[i] == '"') {
      inStr = !inStr;
    } else if (s[i] == ',' && !inStr) {
      if (count < maxCount) {
        out.add(trim(s.substring(start, i)));
        count++;
      }
      start = i + 1;
    }
  }

  // Add the last token
  if (start < s.length) {
    if (count < maxCount) {
      out.add(trim(s.substring(start)));
      count++;
    }
  }
}

// --- Stack Helpers (No limits) ---

void pushIfId(int id) {
  ifStack.add(id);
}

int popIfId() {
  if (ifStack.isNotEmpty) {
    return ifStack.removeLast();
  } else {
    stderr.writeln("Error: 'else' or 'endif' without a preceding 'if'.");
    exit(1);
  }
}

void pushWhileId(int id) {
  whileStack.add(id);
}

int popWhileId() {
  if (whileStack.isNotEmpty) {
    return whileStack.removeLast();
  } else {
    stderr.writeln("Error: 'endwhile' without a preceding 'while'.");
    exit(1);
  }
}

void pushForId(int id) {
  forStack.add(id);
}

int popForId() {
  if (forStack.isNotEmpty) {
    return forStack.removeLast();
  } else {
    stderr.writeln("Error: 'endfor' without a preceding 'for'.");
    exit(1);
  }
}

// --- Main Compiler Logic ---

void compile(String sourcePath, String outputPath) async {
  final inFile = File(sourcePath);
  final outFile = File(outputPath);
  IOSink sink;

  try {
    if (!await inFile.exists()) {
      stderr.writeln('Error: Source file not found: $sourcePath');
      exit(1);
    }
    sink = outFile.openWrite();
  } catch (e) {
    stderr.writeln('Error opening files: $e');
    exit(1);
  }

  final lines = await inFile.readAsLines();

  for (final rawLine in lines) {
    final line = trim(rawLine);
    if (line.isEmpty || line.startsWith('#')) continue;

    // --- CONTROL FLOW STATEMENTS (if/else/endif) ---

    // if(condition_var):
    if (line.endsWith(':')) {
      if (line.startsWith('if(')) {
        final match = RegExp(r'^if\((.+?)\):$').firstMatch(line);
        if (match != null) {
          final condVar = trim(match.group(1)!);
          if (condVar.isNotEmpty) {
            final currentIfId = ifCounter++;
            pushIfId(currentIfId);
            sink.writeln('[0x13] jz $condVar L_ELSE_$currentIfId');
            continue;
          }
        }
      }

      // while(condition_var):
      if (line.startsWith('while(')) {
        final match = RegExp(r'^while\((.+?)\):$').firstMatch(line);
        if (match != null) {
          final condVar = trim(match.group(1)!);
          if (condVar.isNotEmpty) {
            final currentId = whileCounter++;
            pushWhileId(currentId);
            sink.writeln('[0x15] label L_while_START_$currentId');
            sink.writeln('[0x13] jz $condVar L_while_END_$currentId');
            continue;
          }
        }
      }

      // for(condition_var):
      if (line.startsWith('for(')) {
        final match = RegExp(r'^for\((.+?)\):$').firstMatch(line);
        if (match != null) {
          final condVar = trim(match.group(1)!);
          if (condVar.isNotEmpty) {
            final currentId = forCounter++;
            pushForId(currentId);
            sink.writeln('[0x15] label L_for_START_$currentId');
            sink.writeln('[0x13] jz $condVar L_for_END_$currentId');
            continue;
          }
        }
      }
    }

    // else:
    if (line == 'else:') {
      final currentIfId = popIfId();
      pushIfId(currentIfId); // Push back for the 'endif'
      sink.writeln('[0x14] jmp L_ENDIF_$currentIfId');
      sink.writeln('[0x15] label L_ELSE_$currentIfId');
      continue;
    }

    // endif
    if (line == 'endif') {
      final currentIfId = popIfId();
      sink.writeln('[0x15] label L_ELSE_$currentIfId'); // If no 'else' was present
      sink.writeln('[0x15] label L_ENDIF_$currentIfId');
      continue;
    }

    // endwhile
    if (line == 'endwhile') {
      final currentId = popWhileId();
      sink.writeln('[0x14] jmp L_while_START_$currentId');
      sink.writeln('[0x15] label L_while_END_$currentId');
      continue;
    }

    // endfor
    if (line == 'endfor') {
      final currentId = popForId();
      sink.writeln('[0x14] jmp L_for_START_$currentId');
      sink.writeln('[0x15] label L_for_END_$currentId');
      continue;
    }

    // --- EXISTING FUNCTIONALITY ---

    // function entry: "<type> name(params):"
    final funcMatch = RegExp(r'^(\w+)\s+(\w+)\((.*?)\):$').firstMatch(line);
    if (funcMatch != null) {
      final type = funcMatch.group(1)!;
      final name = funcMatch.group(2)!;
      final params = trim(funcMatch.group(3)!);
      
      if (params.isNotEmpty) {
        sink.writeln('[0x01] entry $type $name($params)');
      } else {
        sink.writeln('[0x01] entry $type $name()');
      }
      continue;
    }

    // end
    if (line == 'end') {
      sink.writeln('[0x02] end');
      continue;
    }

    // print(...)
    if (line.startsWith('print(') && line.endsWith(')')) {
      final inside = line.substring(6, line.length - 1);
      final parts = <String>[];
      splitCommas(inside, parts);
      for (final part in parts) {
        sink.writeln('[0x03] stdout $part');
      }
      continue;
    }

    // error("...")
    if (line.startsWith('error(') && line.endsWith(')')) {
      final inside = line.substring(6, line.length - 1);
      sink.writeln('[0x04] stderr $inside');
      continue;
    }

    // input(var)
    if (line.startsWith('input(') && line.endsWith(')')) {
      final varName = trim(line.substring(6, line.length - 1));
      sink.writeln('[0x05] read $varName');
      continue;
    }

    // return <expr>
    if (line.startsWith('return ')) {
      final expr = trim(line.substring(7));
      final parts = expr.split(RegExp(r'\s+'));

      // Use '__ret_type' to convey the type, though the VM handles type promotion.
      final type = parts.length == 3 && (parts[1] == '/' || parts[1] == '^') ? 'double' : 'int'; 
      
      if (parts.length == 3) {
        final a = parts[0];
        final op = parts[1];
        final b = parts[2];

        // Arithmetic/Comparison operators
        switch (op) {
          case '+': sink.writeln('[0x09] add $a $b __ret'); break;
          case '-': sink.writeln('[0x0A] sub $a $b __ret'); break;
          case '*': sink.writeln('[0x0B] mul $a $b __ret'); break;
          case '/': sink.writeln('[0x0C] div $a $b __ret'); break; // VM handles 'double' result
          case '%': sink.writeln('[0x0D] mod $a $b __ret'); break;
          case '^': sink.writeln('[0x0E] pow $a $b __ret'); break; // VM handles 'double' result
          case '>': sink.writeln('[0x0F] gt $a $b __ret'); break;
          case '<': sink.writeln('[0x10] lt $a $b __ret'); break;
          case '==': sink.writeln('[0x11] eq $a $b __ret'); break;
          case '!=': sink.writeln('[0x12] ne $a $b __ret'); break;
          
          // Compound/Logical operators
          case '<=': 
            sink.writeln('[0x0F] gt $a $b __temp_bool');
            sink.writeln('[0x0A] sub 1 __temp_bool __ret'); // NOT (a > b)
            break;
          case '>=':
            sink.writeln('[0x10] lt $a $b __temp_bool');
            sink.writeln('[0x0A] sub 1 __temp_bool __ret'); // NOT (a < b)
            break;
          case '&&': // AND: a * b
            sink.writeln('[0x0B] mul $a $b __ret');
            break;
          case '||': // OR: a + b > 0
            sink.writeln('[0x09] add $a $b __temp_or');
            sink.writeln('[0x0F] gt __temp_or 0 __ret'); 
            break;
            
          default:
            // Fallback to simple store if operator is unknown
            sink.writeln('[0x07] store $type __ret $expr');
        }
      } else {
        // Simple return value
        sink.writeln('[0x07] store $type __ret $expr');
      }
      sink.writeln('[0x06] return_code __ret');
      continue;
    }

    // store: "<type> var = value" or "<type> var = a op b"
    final assignMatch = RegExp(r'^(\w+)\s+(\w+)\s*=\s*(.*)$').firstMatch(line);
    if (assignMatch != null) {
      final type = assignMatch.group(1)!;
      final varName = assignMatch.group(2)!;
      final expression = trim(assignMatch.group(3)!);
      
      // Check for valid type
      if (type != 'int' && type != 'bool' && type != 'string' && type != 'double') {
           stderr.writeln('Compiler Error: Unknown type "$type" on line: $line');
           continue;
      }

      final parts = expression.split(RegExp(r'\s+'));

      if (parts.length == 3) {
        final a = parts[0];
        final op = parts[1];
        final b = parts[2];

        // Arithmetic/Comparison operators
        switch (op) {
          case '+': sink.writeln('[0x09] add $a $b $varName'); break;
          case '-': sink.writeln('[0x0A] sub $a $b $varName'); break;
          case '*': sink.writeln('[0x0B] mul $a $b $varName'); break;
          case '/': sink.writeln('[0x0C] div $a $b $varName'); break;
          case '%': sink.writeln('[0x0D] mod $a $b $varName'); break;
          case '^': sink.writeln('[0x0E] pow $a $b $varName'); break;
          case '>': sink.writeln('[0x0F] gt $a $b $varName'); break;
          case '<': sink.writeln('[0x10] lt $a $b $varName'); break;
          case '==': sink.writeln('[0x11] eq $a $b $varName'); break;
          case '!=': sink.writeln('[0x12] ne $a $b $varName'); break;
          
          // Compound/Logical operators
          case '<=': 
            sink.writeln('[0x0F] gt $a $b ${varName}_temp');
            sink.writeln('[0x0A] sub 1 ${varName}_temp $varName'); // NOT (a > b)
            break;
          case '>=':
            sink.writeln('[0x10] lt $a $b ${varName}_temp');
            sink.writeln('[0x0A] sub 1 ${varName}_temp $varName'); // NOT (a < b)
            break;
          case '&&': // AND: a * b
            sink.writeln('[0x0B] mul $a $b $varName');
            break;
          case '||': // OR: a + b > 0
            sink.writeln('[0x09] add $a $b ${varName}_temp_or');
            sink.writeln('[0x0F] gt ${varName}_temp_or 0 $varName'); 
            break;
            
          default:
            // Fallback to simple store if operator is unknown
            sink.writeln('[0x07] store $type $varName $expression');
        }
      } else {
        // simple store
        sink.writeln('[0x07] store $type $varName $expression');
      }
      continue;
    }

    // call: "func(...)" or "func()" as top-level expression / statement
    final callMatch = RegExp(r'^(\w+)\((.*?)\)$').firstMatch(line);
    if (callMatch != null) {
      final callSignature = callMatch.group(0)!;
      sink.writeln('[0x08] call $callSignature');
      continue;
    }

    // fallback: comment
    sink.writeln('# unknown: $line');
  }

  // Check for open blocks
  if (ifStack.isNotEmpty) {
    stderr.writeln("Error: Missing 'endif' for one or more 'if' blocks.");
    exit(1);
  }
  if (whileStack.isNotEmpty) {
    stderr.writeln("Error: Missing 'endwhile' for one or more 'while' blocks.");
    exit(1);
  }
  if (forStack.isNotEmpty) {
    stderr.writeln("Error: Missing 'endfor' for one or more 'for' blocks.");
    exit(1);
  }

  await sink.close();
  stdout.writeln('Compiled $sourcePath -> $outputPath');
}

void main(List<String> args) {
  if (args.length < 2) {
    stderr.writeln('Usage: dart run compiler.dart source.flux out.fluxb');
    exit(1);
  }
  compile(args[0], args[1]);
}
