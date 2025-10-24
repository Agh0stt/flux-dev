/* compiler.c
   flux -> fluxb compiler.
   Usage: gcc -o compiler compiler.c
          ./compiler source.flux out.fluxb
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// Global state for IF block tracking
// We use a stack to handle nested if blocks.
// The stack stores the unique ID of the current if block.
int if_counter = 0;
int if_stack[32]; // Max 32 nested if blocks
int if_stack_top = -1;

// --- NEW: Global state for LOOP block tracking ---
int while_counter = 0;
int while_stack[32]; // Max 32 nested while blocks
int while_stack_top = -1;

int for_counter = 0;
int for_stack[32]; // Max 32 nested for blocks
int for_stack_top = -1;


void trim(char *s) {
    // trim leading/trailing whitespace in-place
    char *p = s;
    while(*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p)+1);
    int n = strlen(s);
    while(n>0 && isspace((unsigned char)s[n-1])) s[--n] = '\0';
}

int starts_with(const char *s, const char *pref) {
    return strncmp(s, pref, strlen(pref)) == 0;
}

// split comma-separated args into tokens (naive)
void split_commas(char *s, char out[][256], int *count) {
    *count = 0;
    char *p = s;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        char *start = p;
        int in_str = 0;
        while (*p) {
            if (*p == '"' ) in_str = !in_str;
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

// Helper to push an if block ID onto the stack
void push_if_id(int id) {
    if (if_stack_top < 31) {
        if_stack[++if_stack_top] = id;
    } else {
        fprintf(stderr, "Error: Too many nested if blocks.\n");
        exit(1);
    }
}

// Helper to pop an if block ID from the stack
int pop_if_id() {
    if (if_stack_top >= 0) {
        return if_stack[if_stack_top--];
    } else {
        fprintf(stderr, "Error: 'else' or 'endif' without a preceding 'if'.\n");
        exit(1);
    }
}

// --- NEW LOOP STACK HELPERS ---

void push_while_id(int id) {
    if (while_stack_top < 31) {
        while_stack[++while_stack_top] = id;
    } else {
        fprintf(stderr, "Error: Too many nested while blocks.\n");
        exit(1);
    }
}

int pop_while_id() {
    if (while_stack_top >= 0) {
        return while_stack[while_stack_top--];
    } else {
        fprintf(stderr, "Error: 'endwhile' without a preceding 'while'.\n");
        exit(1);
    }
}

void push_for_id(int id) {
    if (for_stack_top < 31) {
        for_stack[++for_stack_top] = id;
    } else {
        fprintf(stderr, "Error: Too many nested for blocks.\n");
        exit(1);
    }
}

int pop_for_id() {
    if (for_stack_top >= 0) {
        return for_stack[for_stack_top--];
    } else {
        fprintf(stderr, "Error: 'endfor' without a preceding 'for'.\n");
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s source.flux out.fluxb\n", argv[0]);
        return 1;
    }
    FILE *fin = fopen(argv[1], "r");
    if (!fin) { perror("open source"); return 1; }
    FILE *fout = fopen(argv[2], "w");
    if (!fout) { perror("open out"); fclose(fin); return 1; }

    char line[1024];
    while (fgets(line, sizeof(line), fin)) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#') continue; // Skip comments starting with #

        // --- CONTROL FLOW STATEMENTS (if/else/endif) ---

        // if(condition_var):
        if (line[strlen(line)-1] == ':') {
            char *if_start = strstr(line, "if"); // Find "if"
            if (if_start == line) { // Must start with 'if'
                char *p_open = strchr(line, '(');
                char *p_close = strchr(line, ')');

                if (p_open && p_close && p_close > p_open) {
                    char cond_var[128];
                    char *start = p_open + 1;
                    int len = p_close - start;
                    strncpy(cond_var, start, len);
                    cond_var[len] = '\0';
                    trim(cond_var);

                    // Ensure there's actual content in the condition
                    if (strlen(cond_var) > 0 && (*(p_close + 1) == ':' || *(p_close + 2) == ':')) {
                        int current_if_id = if_counter++;
                        push_if_id(current_if_id);

                        // If condition_var is 0 (false), jump to the else/end block
                        fprintf(fout, "[0x13] jz %s L_ELSE_%d\n", cond_var, current_if_id);
                        continue;
                    }
                }
            }
        }

        // else:
        if (strcmp(line, "else:") == 0) {
            int current_if_id = pop_if_id();
            push_if_id(current_if_id); // Push back, as we're still in the scope

            // Unconditional jump over the else block to the end
            fprintf(fout, "[0x14] jmp L_ENDIF_%d\n", current_if_id);
            // Define the jump target for the preceding 'if' condition
            fprintf(fout, "[0x15] label L_ELSE_%d\n", current_if_id);
            continue;
        }

        // endif
        if (strcmp(line, "endif") == 0) {
            int current_if_id = pop_if_id();
            // Define the end of the IF block.
            fprintf(fout, "[0x15] label L_ELSE_%d\n", current_if_id);
            fprintf(fout, "[0x15] label L_ENDIF_%d\n", current_if_id);
            continue;
        }

        // --- NEW: LOOP STATEMENTS (while/endwhile & for/endfor) ---

        if (line[strlen(line)-1] == ':') {
            char *p_open = strchr(line, '(');
            char *p_close = strchr(line, ')');

            if (p_open && p_close && p_close > p_open) {
                
                // Extract condition variable
                char cond_var[128];
                char *start = p_open + 1;
                int len = p_close - start;
                strncpy(cond_var, start, len);
                cond_var[len] = '\0';
                trim(cond_var);

                if (strlen(cond_var) > 0 && (*(p_close + 1) == ':' || *(p_close + 2) == ':')) {
                    
                    // while(condition_var):
                    char *while_start = strstr(line, "while"); 
                    if (while_start == line) {
                        int current_id = while_counter++;
                        push_while_id(current_id);

                        // 1. Define the start label
                        fprintf(fout, "[0x15] label L_while_START_%d\n", current_id);
                        
                        // 2. Conditional jump: If condition_var is 0 (false), jump to the end
                        fprintf(fout, "[0x13] jz %s L_while_END_%d\n", cond_var, current_id);
                        
                        continue;
                    }

                    // for(condition_var):
                    char *for_start = strstr(line, "for"); 
                    if (for_start == line) {
                        int current_id = for_counter++;
                        push_for_id(current_id);

                        // 1. Define the start label
                        fprintf(fout, "[0x15] label L_for_START_%d\n", current_id);
                        
                        // 2. Conditional jump: If condition_var is 0 (false), jump to the end
                        fprintf(fout, "[0x13] jz %s L_for_END_%d\n", cond_var, current_id);
                        
                        continue;
                    }
                }
            }
        }

        // endwhile
        if (strcmp(line, "endwhile") == 0) {
            int current_id = pop_while_id();
            
            // 1. Unconditional jump back to the start label
            fprintf(fout, "[0x14] jmp L_while_START_%d\n", current_id);
            
            // 2. Define the end label (jump target for jz)
            fprintf(fout, "[0x15] label L_while_END_%d\n", current_id); 
            continue;
        }

        // endfor
        if (strcmp(line, "endfor") == 0) {
            int current_id = pop_for_id();
            
            // 1. Unconditional jump back to the start label
            fprintf(fout, "[0x14] jmp L_for_START_%d\n", current_id);
            
            // 2. Define the end label (jump target for jz)
            fprintf(fout, "[0x15] label L_for_END_%d\n", current_id); 
            continue;
        }

        // --- EXISTING FUNCTIONALITY ---

        // function entry: "<type> name(params):"
        // e.g. int add(int x, int y):
        char type[64], rest[256];
        if (sscanf(line, "%63s %255[^\n]", type, rest) >= 2) {
            size_t L = strlen(line);
            if (line[L-1] == ':' && strchr(rest, '(') && strchr(rest, ')')) {
                // extract name and params inside parentheses
                char name[128];
                char params[256];
                char *popen = strchr(rest, '(');
                char *pclose = strrchr(rest, ')');
                if (popen && pclose && pclose > popen) {
                    int namelen = popen - rest;
                    while (namelen>0 && isspace((unsigned char)rest[namelen-1])) namelen--;
                    strncpy(name, rest, namelen);
                    name[namelen] = '\0';
                    int plen = pclose - popen - 1;
                    if (plen > 0) {
                        strncpy(params, popen+1, plen);
                        params[plen] = '\0';
                        trim(params);
                    } else params[0] = '\0';
                    trim(name);
                    // write entry with params preserved
                    if (params[0])
                        fprintf(fout, "[0x01] entry %s %s(%s)\n", type, name, params);
                    else
                        fprintf(fout, "[0x01] entry %s %s()\n", type, name);
                    continue;
                }
            }
        }

        // end
        if (strcmp(line, "end") == 0) {
            fprintf(fout, "[0x02] end\n");
            continue;
        }

        // print(...) supports multiple args separated by comma
        if (starts_with(line, "print(") && line[strlen(line)-1] == ')') {
            char inside[900];
            strncpy(inside, line + 6, sizeof(inside)-1);
            inside[sizeof(inside)-1] = '\0';
            inside[strlen(inside)-1] = '\0'; // remove trailing ')'
            trim(inside);
            // split by commas
            char parts[16][256];
            int pc = 0;
            split_commas(inside, parts, &pc);
            for (int i=0;i<pc;i++) {
                fprintf(fout, "[0x03] stdout %s\n", parts[i]);
            }
            continue;
        }

        // error("...")
        if (starts_with(line, "error(") && line[strlen(line)-1] == ')') {
            char inside[900];
            strncpy(inside, line + 6, sizeof(inside)-1);
            inside[sizeof(inside)-1] = '\0';
            inside[strlen(inside)-1] = '\0';
            trim(inside);
            fprintf(fout, "[0x04] stderr %s\n", inside);
            continue;
        }

        // input(var)
        if (starts_with(line, "input(") && line[strlen(line)-1] == ')') {
            char var[128];
            strncpy(var, line + 6, sizeof(var)-1);
            var[sizeof(var)-1] = '\0';
            var[strlen(var)-1] = '\0';
            trim(var);
            fprintf(fout, "[0x05] read %s\n", var);
            continue;
        }

        // return <expr>
        if (starts_with(line, "return ")) {
            char val[512];
            strncpy(val, line + 7, sizeof(val)-1);
            val[sizeof(val)-1] = '\0';
            trim(val);
            // We reuse the arithmetic/comparison parsing logic here for return value calculation
            char a[256], b[256], op[8];
            if (sscanf(val, "%255s %7s %255s", a, op, b) == 3) {
                if (strcmp(op, "+")==0) fprintf(fout, "[0x09] add %s %s __ret\n", a, b);
                else if (strcmp(op, "-")==0) fprintf(fout, "[0x0A] sub %s %s __ret\n", a, b);
                else if (strcmp(op, "*")==0) fprintf(fout, "[0x0B] mul %s %s __ret\n", a, b);
                else if (strcmp(op, "/")==0) fprintf(fout, "[0x0C] div %s %s __ret\n", a, b);
                else if (strcmp(op, "%")==0) fprintf(fout, "[0x0D] mod %s %s __ret\n", a, b);
                else if (strcmp(op, "^")==0) fprintf(fout, "[0x0E] pow %s %s __ret\n", a, b);
                // Comparison operators (less likely for return, but supported for consistency)
                else if (strcmp(op, ">")==0) fprintf(fout, "[0x0F] gt %s %s __ret\n", a, b);
                else if (strcmp(op, "<")==0) fprintf(fout, "[0x10] lt %s %s __ret\n", a, b);
                else if (strcmp(op, "==")==0) fprintf(fout, "[0x11] eq %s %s __ret\n", a, b);
                else if (strcmp(op, "!=")==0) fprintf(fout, "[0x12] ne %s %s __ret\n", a, b);
                else fprintf(fout, "[0x07] store int __ret %s\n", val); // Fallback for unhandled operator
                fprintf(fout, "[0x06] return_code __ret\n");
            } else {
                fprintf(fout, "[0x07] store int __ret %s\n", val);
                fprintf(fout, "[0x06] return_code __ret\n");
            }
            continue;
        }

        // store: "<type> var = value" or "<type> var = a op b"
        {
            char t[64], var[128], eq[4], val[256];
            // Check for assignment pattern: type var = expression
            if (sscanf(line, "%63s %127s %3s %255[^\n]", t, var, eq, val) == 4 && strcmp(eq, "=") == 0) {
                trim(val);
                // check for arithmetic/comparison "a op b"
                char a[256], b[256], op[8];
                if (sscanf(val, "%255s %7s %255s", a, op, b) == 3) {
                    // Arithmetic operators
                    if (strcmp(op, "+")==0) fprintf(fout, "[0x09] add %s %s %s\n", a, b, var);
                    else if (strcmp(op, "-")==0) fprintf(fout, "[0x0A] sub %s %s %s\n", a, b, var);
                    else if (strcmp(op, "*")==0) fprintf(fout, "[0x0B] mul %s %s %s\n", a, b, var);
                    else if (strcmp(op, "/")==0) fprintf(fout, "[0x0C] div %s %s %s\n", a, b, var);
                    else if (strcmp(op, "%")==0) fprintf(fout, "[0x0D] mod %s %s %s\n", a, b, var);
                    else if (strcmp(op, "^")==0) fprintf(fout, "[0x0E] pow %s %s %s\n", a, b, var);
                    // --- COMPARISON OPERATORS ---
                    else if (strcmp(op, ">")==0) fprintf(fout, "[0x0F] gt %s %s %s\n", a, b, var);
                    else if (strcmp(op, "<")==0) fprintf(fout, "[0x10] lt %s %s %s\n", a, b, var);
                    else if (strcmp(op, "==")==0) fprintf(fout, "[0x11] eq %s %s %s\n", a, b, var);
                    else if (strcmp(op, "!=")==0) fprintf(fout, "[0x12] ne %s %s %s\n", a, b, var);
                    // Fallback to simple store if operator is unknown
                    else fprintf(fout, "[0x07] store %s %s %s\n", t, var, val);
                } else {
                    // simple store
                    fprintf(fout, "[0x07] store %s %s %s\n", t, var, val);
                }
                continue;
            }
        }

        // call: "func(...)" or "func()" as top-level expression / statement
        {
            char callname[128];
            if (sscanf(line, "%127[^(\n]", callname) == 1) {
                trim(callname);
                size_t l = strlen(line);
                if (l > 1 && line[l-1] == ')' && strchr(line, '(')) {
                    // take whole content inside parentheses as params (naive)
                    char params[512];
                    char *p = strchr(line, '(');
                    if (p) {
                        strncpy(params, p+1, sizeof(params)-1);
                        params[sizeof(params)-1] = '\0';
                        params[strlen(params)-1] = '\0'; // remove trailing ')'
                        trim(params);
                        if (strlen(params) == 0)
                            fprintf(fout, "[0x08] call %s()\n", callname);
                        else
                            fprintf(fout, "[0x08] call %s(%s)\n", callname, params);
                        continue;
                    }
                }
            }
        }

        // fallback: comment
        fprintf(fout, "# unknown: %s\n", line);
    }

    // Check for open blocks
    if (if_stack_top != -1) {
        fprintf(stderr, "Error: Missing 'endif' for one or more 'if' blocks.\n");
    }
    if (while_stack_top != -1) {
        fprintf(stderr, "Error: Missing 'endwhile' for one or more 'while' blocks.\n");
    }
    if (for_stack_top != -1) {
        fprintf(stderr, "Error: Missing 'endfor' for one or more 'for' blocks.\n");
    }

    fclose(fin);
    fclose(fout);
    printf("Compiled %s -> %s\n", argv[1], argv[2]);
    return 0;
}
