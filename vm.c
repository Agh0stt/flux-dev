/* vm.c
   Flux Bytecode Virtual Machine.
   Usage: gcc -o vm vm.c -lm
          ./vm program.fluxb
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h> // For pow()

#define MAX_INSTRUCTIONS 1024
#define MAX_SYMBOLS 128
#define MAX_LABELS 64
#define MAX_OPERAND_LEN 256
#define MAX_LINE_LEN 1024
#define MAX_CALL_STACK 32 // For function return addresses

// --- Data Structures for the VM ---

// Simple structure for variable storage (Symbol Table Entry)
typedef struct {
    char name[64];
    long value; // Stores int/bool value
    char *s_value; // Stores string value (dynamically allocated)
    char type[16]; // "int", "bool", "string"
    int active;
} SymbolTableEntry;

// Instruction structure
typedef struct {
    int opcode;
    char op_name[16];
    char arg1[MAX_OPERAND_LEN];
    char arg2[MAX_OPERAND_LEN];
    char dest[MAX_OPERAND_LEN]; // Destination variable or label
} Instruction;

// Label mapping
typedef struct {
    char name[64];
    int instr_index; // Index into instructions array
} LabelMap;

// Function mapping structure
typedef struct {
    char name[64];
    int instr_index; // Instruction index of the [0x01] entry
    char params[MAX_OPERAND_LEN]; // Parameter declaration string (e.g., "int x, int y")
} FunctionMapEntry;


// Global state
Instruction instructions[MAX_INSTRUCTIONS];
int instr_count = 0;

SymbolTableEntry symbol_table[MAX_SYMBOLS];
int symbol_count = 0;

LabelMap label_map[MAX_LABELS];
int label_count = 0;

FunctionMapEntry function_map[64];
int function_count = 0;

int call_stack[MAX_CALL_STACK];
int stack_top = -1; // Call stack pointer

int main_entry_point = -1;

// --- Utility Functions ---
// --- NEW UTILITY FUNCTION ---
// Replaces only the '\n' escape sequence in a string
void unescape_newline(char *s) {
    char *p = s;
    char *q = s;
    while (*p) {
        if (*p == '\\' && *(p + 1) == 'n') {
            *q++ = '\n';
            p += 2; // Skip both '\' and 'n'
        } else {
            *q++ = *p++;
        }
    }
    *q = '\0'; // Null-terminate the new string
}

void trim(char *s) {
    char *p = s;
    while(*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    int n = strlen(s);
    while(n>0 && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}

// Check if a string is a variable name (starts with a letter and not a quoted string)
int is_variable(const char *s) {
    if (!s || s[0] == '\0') return 0;
    if (s[0] == '"') return 0; // It's a string literal
    return isalpha((unsigned char)s[0]);
}

// Find a variable by name in the symbol table
SymbolTableEntry* get_symbol(const char *name) {
    for (int i = 0; i < symbol_count; i++) {
        if (symbol_table[i].active && strcmp(symbol_table[i].name, name) == 0) {
            return &symbol_table[i];
        }
    }
    return NULL;
}

// Get the numerical value of an operand (either literal or variable)
long get_long_value(const char *operand) {
    if (is_variable(operand)) {
        SymbolTableEntry *s = get_symbol(operand);
        if (s && (strcmp(s->type, "int") == 0 || strcmp(s->type, "bool") == 0)) {
            return s->value;
        }
        fprintf(stderr, "VM Error: Undefined or non-numeric variable '%s'.\n", operand);
        exit(1);
    }
    // Assume it's a numeric literal
    return strtol(operand, NULL, 10);
}

// Get the string value of an operand (either literal or variable)
char* get_string_value(const char *operand) {
    if (operand[0] == '"') {
        // String literal: remove quotes
        char *str = strdup(operand + 1);
        if (str[strlen(str) - 1] == '"') str[strlen(str) - 1] = '\0';
        return str;
    }

    if (is_variable(operand)) {
        SymbolTableEntry *s = get_symbol(operand);
        if (s && strcmp(s->type, "string") == 0 && s->s_value) {
            return strdup(s->s_value); // Return a copy
        }
    }

    // Not a string variable or literal
    return strdup("");
}


// Set the value of a destination variable
void set_symbol_value(const char *name, const char *type, long val, const char *s_val) {
    SymbolTableEntry *s = get_symbol(name);

    // If symbol doesn't exist, create it
    if (!s) {
        if (symbol_count >= MAX_SYMBOLS) {
            fprintf(stderr, "VM Error: Symbol table overflow.\n");
            exit(1);
        }
        s = &symbol_table[symbol_count++];
        strncpy(s->name, name, sizeof(s->name) - 1);
        s->active = 1;
        s->s_value = NULL;
    }

    // Set type and value
    strncpy(s->type, type, sizeof(s->type) - 1);
    s->value = val;

    // Handle string values
    if (s_val && strcmp(type, "string") == 0) {
        if (s->s_value) free(s->s_value);
        s->s_value = strdup(s_val);
    } else if (s->s_value) {
        free(s->s_value);
        s->s_value = NULL;
    }
}

// Find instruction index for a label
int find_label(const char *name) {
    for (int i = 0; i < label_count; i++) {
        if (strcmp(label_map[i].name, name) == 0) {
            return label_map[i].instr_index;
        }
    }
    fprintf(stderr, "VM Error: Label '%s' not found.\n", name);
    return -1;
}

// Find function entry by name
FunctionMapEntry* find_function(const char *name) {
    for (int i = 0; i < function_count; i++) {
        if (strcmp(function_map[i].name, name) == 0) {
            return &function_map[i];
        }
    }
    return NULL;
}


// --- Bytecode Loading ---

void load_bytecode(const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) { perror("open bytecode file"); exit(1); }

    char line[MAX_LINE_LEN];
    while (fgets(line, sizeof(line), f)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue;

        if (instr_count >= MAX_INSTRUCTIONS) {
            fprintf(stderr, "VM Error: Instruction buffer overflow.\n");
            break;
        }

        Instruction *instr = &instructions[instr_count];

        // Parse Opcode ID and Name
        int n_parsed = sscanf(line, "[%x] %15s", &instr->opcode, instr->op_name);
        if (n_parsed != 2) continue;

        // Skip the Opcode part to get to operands
        char *op_start = strchr(line, ']');
        if (!op_start) continue;

        // Find the start of the operand string (after the opcode name and space)
        char *arg_start = op_start + strlen(instr->op_name) + 3;
        if (arg_start >= line + strlen(line)) arg_start = "";

        // Copy arguments based on opcode type
        instr->arg1[0] = instr->arg2[0] = instr->dest[0] = '\0';

        switch (instr->opcode) {
            case 0x01: { // entry <type> <name>(<params>)
                // Example: int add(int x, int y)
                char func_type[16], func_name_paren[64];
                if (sscanf(arg_start, "%15s %63[^\n]", func_type, func_name_paren) == 2) {
                    char *popen = strchr(func_name_paren, '(');
                    char *pclose = strrchr(func_name_paren, ')');

                    if (popen && pclose && pclose > popen) {
                        int name_len = popen - func_name_paren;
                        // Extract function name
                        strncpy(function_map[function_count].name, func_name_paren, name_len);
                        function_map[function_count].name[name_len] = '\0';
                        trim(function_map[function_count].name);

                        // Extract parameter declaration string
                        int param_len = pclose - popen - 1;
                        if (param_len > 0) {
                            strncpy(function_map[function_count].params, popen + 1, param_len);
                            function_map[function_count].params[param_len] = '\0';
                            trim(function_map[function_count].params);
                        } else {
                            function_map[function_count].params[0] = '\0';
                        }
                        
                        function_map[function_count].instr_index = instr_count;
                        
                        // Check for main entry point
                        if (strcmp(function_map[function_count].name, "main") == 0) {
                            main_entry_point = instr_count;
                        }

                        function_count++;
                    }
                }
                break;
            }
            case 0x03: // stdout <value>
            case 0x04: // stderr <value>
            case 0x05: // read <var>
            case 0x06: // return_code <var>
            case 0x08: // call <name>(<params>)
                // For call, arg1 stores the full call signature: func(a,b)
                strncpy(instr->arg1, arg_start, sizeof(instr->arg1)-1);
                trim(instr->arg1);
                break;
            case 0x07: // store <type> <var> <value>
                // We need custom parsing to handle string literals in <value>
                char type_buf[16], var_buf[64];
                if (sscanf(arg_start, "%15s %63s", type_buf, var_buf) == 2) {
                    strncpy(instr->arg1, type_buf, sizeof(instr->arg1) - 1); // type
                    strncpy(instr->arg2, var_buf, sizeof(instr->arg2) - 1); // var
                    char *val_start = strstr(arg_start, var_buf) + strlen(var_buf);
                    if (val_start) {
                        // Advance past whitespace
                        while (*val_start && isspace((unsigned char)*val_start)) val_start++;
                        strncpy(instr->dest, val_start, sizeof(instr->dest) - 1); // value
                        trim(instr->dest);
                    }
                }
                break;
            case 0x13: // jz <cond_var> <label>
                sscanf(arg_start, "%s %s", instr->arg1, instr->dest);
                break;
            case 0x14: // jmp <label>
                sscanf(arg_start, "%s", instr->dest);
                break;
            case 0x15: { // label <name>
                char label_name[64];
                if (sscanf(arg_start, "%s", label_name) == 1) {
                    if (label_count >= MAX_LABELS) {
                        fprintf(stderr, "VM Error: Label map overflow.\n");
                        exit(1);
                    }
                    strncpy(label_map[label_count].name, label_name, sizeof(label_map[label_count].name) - 1);
                    label_map[label_count].instr_index = instr_count;
                    label_count++;
                }
                break;
            }
            // All binary operators (add, sub, mul, div, mod, pow, gt, lt, eq, ne)
            case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E:
            case 0x0F: case 0x10: case 0x11: case 0x12:
                sscanf(arg_start, "%s %s %s", instr->arg1, instr->arg2, instr->dest);
                break;
        }
        instr_count++;
    }
    fclose(f);
}

// Splits a comma-separated string of variable declarations (e.g., "int x, string s")
// or argument values (e.g., "a, "hello", 5") into an array of tokens.
void split_commas(const char *s, char out[][MAX_OPERAND_LEN], int *count) {
    *count = 0;
    const char *p = s;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        const char *start = p;
        int in_str = 0;
        while (*p) {
            if (*p == '"') in_str = !in_str;
            if (!in_str && *p == ',') break;
            p++;
        }
        int len = p - start;
        if (len > 0 && *count < 32) {
            strncpy(out[*count], start, len);
            out[*count][len] = '\0';
            trim(out[*count]);
            (*count)++;
        }
        if (*p == ',') p++;
    }
}


// --- VM Execution ---

void execute_vm() {
    if (main_entry_point == -1) {
        fprintf(stderr, "VM Error: Program does not contain an 'int main()' entry point.\n");
        return;
    }

    int pc = main_entry_point + 1; // Program Counter starts after 'entry'
    long ret_val = 0;

    // Execution loop
    while (pc < instr_count) {
        Instruction *instr = &instructions[pc];

        // Skip 'label' instructions, they are only targets
        if (instr->opcode == 0x15) {
            pc++;
            continue;
        }

        long op1_val, op2_val;
        SymbolTableEntry *dest_sym;

        switch (instr->opcode) {
            case 0x02: // end (Only reached if returning from main)
                
                return;

            
case 0x03: { // stdout <value>
    char *s_val = NULL;
    if (instr->arg1[0] == '"') {
        // Handle string literal:
        s_val = get_string_value(instr->arg1);
        
        // VITAL CHANGE: Unescape ONLY the newline sequence
        unescape_newline(s_val); 
        
        printf("%s", s_val);
        free(s_val);
    } else if (is_variable(instr->arg1)) {
        // Handle variable printing (no change)
        SymbolTableEntry *s = get_symbol(instr->arg1);
        if (s) {
            if (strcmp(s->type, "string") == 0 && s->s_value) {
                printf("%s", s->s_value);
            } else if (strcmp(s->type, "int") == 0 || strcmp(s->type, "bool") == 0) {
                printf("%ld", s->value);
            } else {
                printf("<unsupported type>");
            }
        } else {
            fprintf(stderr, "VM Error: Cannot print undefined variable '%s'.\n", instr->arg1);
        }
    } else {
        // Numeric literal
        printf("%s", instr->arg1);
    }
    break;
}


            case 0x04: // stderr <value> (Same logic as stdout, but uses stderr)
                if (instr->arg1[0] == '"') {
                    fprintf(stderr, "%s", instr->arg1 + 1);
                } else {
                    fprintf(stderr, "%s", instr->arg1);
                }
                break;

            
            case 0x05: { // read <var>
    			char input_buffer[256];
    if (fgets(input_buffer, sizeof(input_buffer), stdin)) {
        input_buffer[strcspn(input_buffer, "\n")] = '\0'; // remove newline

        char *endptr;
        long num = strtol(input_buffer, &endptr, 10); // Use strtol for long

        if (*endptr == '\0') {
            // Pure integer input
            set_symbol_value(instr->arg1, "int", num, NULL);
        } else {
            // String input (or float/malformed if using strtol)
            // Note: If the user enters a float, it will be truncated by strtol
            set_symbol_value(instr->arg1, "string", 0, input_buffer);
        }
    } else {
        fprintf(stderr, "VM Error: Failed to read input.\n");
        exit(1);
    }
    break;
}

            case 0x06: { // return_code <var>
                dest_sym = get_symbol(instr->arg1);
                if (dest_sym) {
                    ret_val = dest_sym->value;
                }

                if (stack_top >= 0) {
                    // Function return: Pop return address and jump
                    pc = call_stack[stack_top--];
                    continue; // Skip pc++ below
                } else {
                    
                    return; 
                }
                break;
            }

            case 0x07: { // store <type> <var> <value>
                // Handle string literals, e.g., "hello"
                if (instr->dest[0] == '"') {
                    char *str_val = get_string_value(instr->dest);
                    set_symbol_value(instr->arg2, instr->arg1, 0, str_val);
                    free(str_val);
                } else {
                    // Numeric literal or variable copy (only works for int/bool)
                    long val = get_long_value(instr->dest);
                    set_symbol_value(instr->arg2, instr->arg1, val, NULL);
                }
                break;
            }
            
            case 0x08: { // call <name>(<params>)
                if (stack_top >= MAX_CALL_STACK - 1) {
                    fprintf(stderr, "VM Error: Call stack overflow.\n");
                    exit(1);
                }

                // 1. Extract function name and arguments passed
                char call_signature[MAX_OPERAND_LEN];
                strncpy(call_signature, instr->arg1, sizeof(call_signature)-1);

                char *popen = strchr(call_signature, '(');
                char *pclose = strrchr(call_signature, ')');

                if (!popen || !pclose || pclose <= popen) {
                    fprintf(stderr, "VM Error: Malformed call signature: %s\n", call_signature);
                    exit(1);
                }

                int name_len = popen - call_signature;
                char func_name[64];
                strncpy(func_name, call_signature, name_len);
                func_name[name_len] = '\0';
                trim(func_name);

                // Arguments passed in the call (e.g., "a, 5, "test"")
                char arg_values_str[MAX_OPERAND_LEN];
                int arg_len = pclose - popen - 1;
                strncpy(arg_values_str, popen + 1, arg_len);
                arg_values_str[arg_len] = '\0';
                trim(arg_values_str);

                FunctionMapEntry *func_entry = find_function(func_name);
                if (!func_entry) {
                    fprintf(stderr, "VM Error: Function '%s' not found.\n", func_name);
                    exit(1);
                }

                // 2. Parse arguments and parameters for passing
                char arg_values[32][MAX_OPERAND_LEN];
                int arg_count = 0;
                split_commas(arg_values_str, arg_values, &arg_count);

                // Parameters declared in the function entry (e.g., "int x, int y")
                char param_tokens[32][MAX_OPERAND_LEN];
                int param_count = 0;
                split_commas(func_entry->params, param_tokens, &param_count);
                
                if (arg_count != param_count) {
                    fprintf(stderr, "VM Error: Function '%s' called with %d arguments, expected %d.\n", func_name, arg_count, param_count);
                    exit(1);
                }

                // 3. Parameter assignment (pass by value)
                for (int i = 0; i < param_count; i++) {
                    char type[16], param_name[64];
                    // Parse declared parameter (e.g., "int x" -> type="int", name="x")
                    if (sscanf(param_tokens[i], "%15s %63s", type, param_name) == 2) {
                        const char *arg_val_token = arg_values[i];
                        
                        // Pass value into the parameter variable
                        if (strcmp(type, "string") == 0) {
                            char *s_val = get_string_value(arg_val_token);
                            set_symbol_value(param_name, type, 0, s_val);
                            free(s_val);
                        } else {
                            long val = get_long_value(arg_val_token);
                            set_symbol_value(param_name, type, val, NULL);
                        }
                    } else {
                        fprintf(stderr, "VM Error: Malformed parameter declaration in function '%s'.\n", func_name);
                        exit(1);
                    }
                }

                // 4. Save return address and jump
                call_stack[++stack_top] = pc + 1;
                pc = func_entry->instr_index + 1; // Jump after the 'entry' instruction
                continue; // Skip pc++
            }

			// Binary Arithmetic Operations (0x09 - 0x0E)
case 0x09: op1_val = get_long_value(instr->arg1); op2_val = get_long_value(instr->arg2); set_symbol_value(instr->dest, "int", op1_val + op2_val, NULL); break; // ADD
case 0x0A: op1_val = get_long_value(instr->arg1); op2_val = get_long_value(instr->arg2); set_symbol_value(instr->dest, "int", op1_val - op2_val, NULL); break; // SUB
case 0x0B: op1_val = get_long_value(instr->arg1); op2_val = get_long_value(instr->arg2); set_symbol_value(instr->dest, "int", op1_val * op2_val, NULL); break; // MUL
case 0x0C: op1_val = get_long_value(instr->arg1); op2_val = get_long_value(instr->arg2); 
           if (op2_val == 0) { fprintf(stderr, "VM Error: Division by zero.\n"); exit(1); }
           set_symbol_value(instr->dest, "int", op1_val / op2_val, NULL); break; // DIV
case 0x0D: op1_val = get_long_value(instr->arg1); op2_val = get_long_value(instr->arg2); set_symbol_value(instr->dest, "int", op1_val % op2_val, NULL); break; // MOD
case 0x0E: op1_val = get_long_value(instr->arg1); op2_val = get_long_value(instr->arg2); set_symbol_value(instr->dest, "int", (long)round(pow((double)op1_val, (double)op2_val)), NULL); break; // POW

            // Comparison Operations (0x0F - 0x12). Result is 1 (true) or 0 (false).
            case 0x0F: op1_val = get_long_value(instr->arg1); op2_val = get_long_value(instr->arg2); set_symbol_value(instr->dest, "bool", (op1_val > op2_val) ? 1 : 0, NULL); break;
            case 0x10: op1_val = get_long_value(instr->arg1); op2_val = get_long_value(instr->arg2); set_symbol_value(instr->dest, "bool", (op1_val < op2_val) ? 1 : 0, NULL); break;
            case 0x11: op1_val = get_long_value(instr->arg1); op2_val = get_long_value(instr->arg2); set_symbol_value(instr->dest, "bool", (op1_val == op2_val) ? 1 : 0, NULL); break;
            case 0x12: op1_val = get_long_value(instr->arg1); op2_val = get_long_value(instr->arg2); set_symbol_value(instr->dest, "bool", (op1_val != op2_val) ? 1 : 0, NULL); break;

            // Control Flow Jumps
            case 0x13: { // jz <cond_var> <label> (Jump if Zero/False)
                long condition = get_long_value(instr->arg1);
                if (condition == 0) {
                    int target_pc = find_label(instr->dest);
                    if (target_pc != -1) pc = target_pc;
                    continue; // Skip pc++ below
                }
                break;
            }
            case 0x14: { // jmp <label> (Unconditional Jump)
                int target_pc = find_label(instr->dest);
                if (target_pc != -1) pc = target_pc;
                continue; // Skip pc++ below
            }

            case 0x01: // entry: Already handled by finding the jump target.
                break;

            default:
                fprintf(stderr, "VM Warning: Unhandled opcode 0x%X at instruction %d.\n", instr->opcode, pc);
        }

        pc++; // Advance Program Counter
    }
    
}

// Main VM execution logic
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s program.fluxb\n", argv[0]);
        return 1;
    }

    load_bytecode(argv[1]);
    execute_vm();

    // Clean up allocated strings
    for (int i = 0; i < symbol_count; i++) {
        if (symbol_table[i].s_value) {
            free(symbol_table[i].s_value);
        }
    }

    return 0;
}
