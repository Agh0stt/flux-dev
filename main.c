/* compiler.c
   flux -> fluxb compiler.
   Usage: gcc -o compiler compiler.c
          ./compiler source.flux out.fluxb
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

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
        if (line[0] == '\0') continue;

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
            // if expression a + b -> compile to op then return_code __ret
            // we output the expression as a store to __ret using arithmetic opcodes if possible
            char a[256], b[256], op[8];
            if (sscanf(val, "%255s %7s %255s", a, op, b) == 3) {
                if (strcmp(op, "+")==0) fprintf(fout, "[0x09] add %s %s __ret\n", a, b);
                else if (strcmp(op, "-")==0) fprintf(fout, "[0x0A] sub %s %s __ret\n", a, b);
                else if (strcmp(op, "*")==0) fprintf(fout, "[0x0B] mul %s %s __ret\n", a, b);
                else if (strcmp(op, "/")==0) fprintf(fout, "[0x0C] div %s %s __ret\n", a, b);
                else if (strcmp(op, "%")==0) fprintf(fout, "[0x0D] mod %s %s __ret\n", a, b);
                else fprintf(fout, "[0x07] store int __ret %s\n", val);
                fprintf(fout, "[0x06] return_code __ret\n");
            } else {
                fprintf(fout, "[0x07] store int __ret %s\n", val);
                fprintf(fout, "[0x06] return_code __ret\n");
            }
            continue;
        }

        // store: "<type> var = value" or "<type> var = a + b"
        {
            char t[64], var[128], eq[4], val[256];
            if (sscanf(line, "%63s %127s %3s %255[^\n]", t, var, eq, val) == 4 && strcmp(eq, "=") == 0) {
                trim(val);
                // check for arithmetic "a + b"
                char a[256], b[256], op[8];
                if (sscanf(val, "%255s %7s %255s", a, op, b) == 3) {
                    if (strcmp(op, "+")==0) fprintf(fout, "[0x09] add %s %s %s\n", a, b, var);
                    else if (strcmp(op, "-")==0) fprintf(fout, "[0x0A] sub %s %s %s\n", a, b, var);
                    else if (strcmp(op, "*")==0) fprintf(fout, "[0x0B] mul %s %s %s\n", a, b, var);
                    else if (strcmp(op, "/")==0) fprintf(fout, "[0x0C] div %s %s %s\n", a, b, var);
                    else if (strcmp(op, "%")==0) fprintf(fout, "[0x0D] mod %s %s %s\n", a, b, var);
                    else if (strcmp(op, "^")==0) fprintf(fout, "[0x0E] pow %s %s %s\n", a, b, var);
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

    fclose(fin);
    fclose(fout);
    printf("Compiled %s -> %s\n", argv[1], argv[2]);
    return 0;
}
