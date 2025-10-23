/* vm.c
   VM for flux bytecode emitted by compiler.c.
   Usage: gcc -o vm vm.c -lm
          ./vm out.fluxb
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define MAX_LINES 20000
#define MAX_VARS 4096
#define MAX_FUNCS 512
#define MAX_CALLSTACK 1024
#define MAX_PARAM_COUNT 32

typedef enum {VT_INT, VT_DOUBLE, VT_BOOL, VT_STR, VT_VOID, VT_UNKNOWN} vtype;

typedef struct {
    char name[128];
    vtype type;
    long long ival;
    double dval;
    char *sval; // heap allocated for strings
    int used;
} Var;

typedef struct {
    char name[128];
    int line_index; // index in lines[] where entry occurs
    char params[ MAX_PARAM_COUNT ][128];
    int param_count;
} Func;

char *lines[MAX_LINES];
int nlines = 0;

Func funcs[MAX_FUNCS];
int nfuncs = 0;

Var vars[MAX_VARS];
int nvars = 0;

int callstack[MAX_CALLSTACK];
int callstack_top = 0;

void trim(char *str) {
    char *s = str;
    while (*s && isspace((unsigned char)*s)) s++;
    if (s != str) memmove(str, s, strlen(s)+1);
    int L = strlen(str);
    while (L>0 && isspace((unsigned char)str[L-1])) str[--L] = '\0';
}

int push_ret(int v) {
    if (callstack_top >= MAX_CALLSTACK) return -1;
    callstack[callstack_top++] = v;
    return 0;
}
int pop_ret() {
    if (callstack_top <= 0) return -1;
    return callstack[--callstack_top];
}

Var* find_var(const char *name) {
    for (int i=0;i<nvars;i++) if (strcmp(vars[i].name, name)==0) return &vars[i];
    return NULL;
}
Var* create_var(const char *name, vtype t) {
    Var *v = find_var(name);
    if (v) return v;
    if (nvars >= MAX_VARS) return NULL;
    strncpy(vars[nvars].name, name, sizeof(vars[nvars].name)-1);
    vars[nvars].type = t;
    vars[nvars].used = 1;
    vars[nvars].ival = 0;
    vars[nvars].dval = 0.0;
    vars[nvars].sval = NULL;
    nvars++;
    return &vars[nvars-1];
}
void set_var_from_literal(Var *v, const char *tok) {
    if (!v) return;
    if (!tok) return;
    // string literal
    if (tok[0] == '"' && tok[strlen(tok)-1] == '"' && strlen(tok) >= 2) {
        v->type = VT_STR;
        free(v->sval);
        v->sval = strdup(tok+1);
        v->sval[strlen(v->sval)-1] = '\0';
        return;
    }
    // bool
    if (strcasecmp(tok,"true")==0 || strcasecmp(tok,"false")==0) {
        v->type = VT_BOOL;
        v->ival = (strcasecmp(tok,"true")==0) ? 1 : 0;
        return;
    }
    // number detection
    int isnum = 1;
    int hasdot = 0;
    const char *p = tok;
    if (*p=='+'||*p=='-') p++;
    while (*p) {
        if (*p == '.') { if (hasdot) { isnum = 0; break; } hasdot = 1; }
        else if (!isdigit((unsigned char)*p)) { isnum = 0; break; }
        p++;
    }
    if (isnum) {
        if (hasdot) {
            v->type = VT_DOUBLE;
            v->dval = atof(tok);
        } else {
            v->type = VT_INT;
            v->ival = atoll(tok);
        }
        return;
    }
    // otherwise treat as string
    v->type = VT_STR;
    free(v->sval);
    v->sval = strdup(tok);
}

int find_func(const char *name) {
    for (int i=0;i<nfuncs;i++) if (strcmp(funcs[i].name, name)==0) return i;
    return -1;
}

void load_lines(const char *fname) {
    FILE *f = fopen(fname, "r");
    if (!f) { perror("open bytecode"); exit(1); }
    char buf[2048];
    while (fgets(buf, sizeof(buf), f)) {
        size_t L = strlen(buf);
        while (L>0 && (buf[L-1]=='\n' || buf[L-1]=='\r')) { buf[--L] = '\0'; }
        lines[nlines] = strdup(buf);
        nlines++;
        if (nlines >= MAX_LINES) break;
    }
    fclose(f);
}

void parse_entry_params(Func *fn, const char *p) {
    // p points to after 'entry' token, e.g. "int add(x, y)"
    // we extract name and param names
    char type[64];
    char rest[256];
    if (sscanf(p, "%63s %255[^\n]", type, rest) >= 2) {
        // rest like "add(x, y)" or "add()"
        char *par = strchr(rest, '(');
        if (!par) return;
        int namelen = par - rest;
        while (namelen>0 && isspace((unsigned char)rest[namelen-1]) ) namelen--;
        char fname[128];
        strncpy(fname, rest, namelen);
        fname[namelen] = '\0';
        trim(fname);
        strncpy(fn->name, fname, sizeof(fn->name)-1);
        fn->param_count = 0;
        char *pclose = strchr(par, ')');
        if (!pclose) return;
        int plen = pclose - par - 1;
        if (plen <= 0) return;
        char tmp[256];
        strncpy(tmp, par+1, plen);
        tmp[plen] = '\0';
        // split by comma, extract variable names (type may be included)
        char *tok = strtok(tmp, ",");
        while (tok && fn->param_count < MAX_PARAM_COUNT) {
            trim(tok);
            // tok may be "int x" or "x"
            char pname[128];
            char ptype[64];
            if (sscanf(tok, "%63s %127s", ptype, pname) == 2) {
                // ok
            } else {
                strncpy(pname, tok, sizeof(pname)-1);
                pname[sizeof(pname)-1] = '\0';
            }
            trim(pname);
            strncpy(fn->params[fn->param_count], pname, 127);
            fn->param_count++;
            tok = strtok(NULL, ",");
        }
    }
}

void build_func_table() {
    for (int i=0;i<nlines;i++) {
        char *ln = lines[i];
        if (!ln) continue;
        // look for "[0x01] entry"
        char *p = strstr(ln, "[0x01]");
        if (p == ln) {
            p += strlen("[0x01]");
            while (*p && isspace((unsigned char)*p)) p++;
            if (strncmp(p, "entry", 5)==0) {
                p += 5;
                while (*p && isspace((unsigned char)*p)) p++;
                if (nfuncs < MAX_FUNCS) {
                    funcs[nfuncs].line_index = i;
                    // parse name and params
                    parse_entry_params(&funcs[nfuncs], p);
                    // if parse_entry_params didn't fill name (maybe different format), use fallback
                    if (funcs[nfuncs].name[0] == '\0') {
                        // try naive parse
                        char type[64], name[128];
                        if (sscanf(p, "%63s %127[^ (](%*[^)])", type, name) >= 1) {
                            trim(name);
                            strncpy(funcs[nfuncs].name, name, sizeof(funcs[nfuncs].name)-1);
                        }
                    }
                    nfuncs++;
                }
            }
        }
    }
}

// helper: evaluate token to variable value; returns a Var created if literal
Var* resolve_token_as_var(const char *tok) {
    if (!tok) return NULL;
    char tmp[512];
    strncpy(tmp, tok, sizeof(tmp)-1);
    tmp[sizeof(tmp)-1] = '\0';
    trim(tmp);
    if (tmp[0] == '\0') return NULL;
    Var *v = find_var(tmp);
    if (v) return v;
    // if literal number/string/bool -> create temporary var
    Var *tv = create_var("__lit_tmp", VT_UNKNOWN);
    set_var_from_literal(tv, tmp);
    return tv;
}

void assign_value_to_varname(const char *name, Var *src) {
    if (!name || !src) return;
    Var *dst = find_var(name);
    if (!dst) dst = create_var(name, src->type);
    if (!dst) return;
    // copy value
    dst->type = src->type;
    dst->ival = src->ival;
    dst->dval = src->dval;
    free(dst->sval);
    dst->sval = src->sval ? strdup(src->sval) : NULL;
}

void exec_arith(const char *op, const char *a_tok, const char *b_tok, const char *dest) {
    Var *a = resolve_token_as_var(a_tok);
    Var *b = resolve_token_as_var(b_tok);
    if (!a || !b) return;
    Var *d = find_var(dest);
    if (!d) d = create_var(dest, VT_UNKNOWN);

    // detect types
    int a_is_double = (a->type == VT_DOUBLE);
    int b_is_double = (b->type == VT_DOUBLE);

    // promote integer to double if necessary
    if (a_is_double || b_is_double || strcmp(op,"div")==0) {
        double av = (a->type == VT_DOUBLE) ? a->dval : (double)a->ival;
        double bv = (b->type == VT_DOUBLE) ? b->dval : (double)b->ival;
        if (strcmp(op, "add")==0) d->dval = av + bv;
        else if (strcmp(op, "sub")==0) d->dval = av - bv;
        else if (strcmp(op, "mul")==0) d->dval = av * bv;
        else if (strcmp(op, "div")==0) d->dval = (bv == 0.0) ? 0.0 : av / bv;
        else if (strcmp(op, "mod")==0) d->dval = fmod(av, bv);
        else if (strcmp(op, "pow")==0) d->dval = pow(av, bv);
        d->type = VT_DOUBLE;
    } else {
        long long av = a->ival;
        long long bv = b->ival;
        if (strcmp(op, "add")==0) d->ival = av + bv;
        else if (strcmp(op, "sub")==0) d->ival = av - bv;
        else if (strcmp(op, "mul")==0) d->ival = av * bv;
        else if (strcmp(op, "div")==0) d->dval = (double)av / (double)bv; // promote division to double
        else if (strcmp(op, "mod")==0) d->ival = (bv==0)?0:av%bv;
        else if (strcmp(op, "pow")==0) d->ival = (long long)llround(pow((double)av, (double)bv));
        if (strcmp(op,"div")==0) d->type = VT_DOUBLE;
        else d->type = VT_INT;
    }
}

void execute_bytecode() {
    // find main func index
    int main_idx = find_func("main");
    if (main_idx < 0) {
        fprintf(stderr, "No main() found\n");
        return;
    }
    int ip = funcs[main_idx].line_index + 1;

    while (ip >= 0 && ip < nlines) {
        char *ln = lines[ip];
        if (!ln) { ip++; continue; }
        if (ln[0] == '#' || ln[0] == '\0') { ip++; continue; }
        // locate ']' then opcode token
        char *p = strchr(ln, ']');
        if (!p) { ip++; continue; }
        p++;
        while (*p && isspace((unsigned char)*p)) p++;
        char op[64];
        if (sscanf(p, "%63s", op) != 1) { ip++; continue; }

        if (strcmp(op, "entry")==0) { ip++; continue; }
        else if (strcmp(op, "end")==0) {
            int retaddr = pop_ret();
            if (retaddr == -1) { // end of program
                break;
            } else {
                ip = retaddr;
                continue;
            }
        } else if (strcmp(op, "stdout")==0 || strcmp(op,"stderr")==0) {
            char rest[1024];
            // grab rest of line after token
            const char *tokstart = p + strlen(op);
            strncpy(rest, tokstart, sizeof(rest)-1); rest[sizeof(rest)-1] = '\0';
            trim(rest);
            // if string literal (starts with "), print str
            if (rest[0] == '"' && rest[strlen(rest)-1] == '"' && strlen(rest) >= 2) {
                // print without quotes
                char tmp[1024];
                strncpy(tmp, rest+1, sizeof(tmp)-2);
                tmp[sizeof(tmp)-2] = '\0';
                tmp[strlen(tmp)-1] = '\0';
                if (strcmp(op,"stdout")==0) printf("%s\n", tmp);
                else fprintf(stderr, "%s\n", tmp);
            } else {
                // token could be var or literal
                Var *v = find_var(rest);
                if (v) {
                    if (v->type == VT_INT) { if (strcmp(op,"stdout")==0) printf("%lld\n", v->ival); else fprintf(stderr,"%lld\n", v->ival); }
                    else if (v->type == VT_DOUBLE) { if (strcmp(op,"stdout")==0) printf("%g\n", v->dval); else fprintf(stderr,"%g\n", v->dval); }
                    else if (v->type == VT_BOOL) { if (strcmp(op,"stdout")==0) printf("%s\n", v->ival ? "true":"false"); else fprintf(stderr,"%s\n", v->ival ? "true":"false"); }
                    else if (v->type == VT_STR) { if (strcmp(op,"stdout")==0) printf("%s\n", v->sval ? v->sval : ""); else fprintf(stderr,"%s\n", v->sval ? v->sval : ""); }
                    else { if (strcmp(op,"stdout")==0) printf("0\n"); else fprintf(stderr,"0\n"); }
                } else {
                    // maybe literal
                    Var tv;
                    memset(&tv,0,sizeof(tv));
                    set_var_from_literal(&tv, rest);
                    if (tv.type == VT_INT) { if (strcmp(op,"stdout")==0) printf("%lld\n", tv.ival); else fprintf(stderr,"%lld\n", tv.ival); }
                    else if (tv.type == VT_DOUBLE) { if (strcmp(op,"stdout")==0) printf("%g\n", tv.dval); else fprintf(stderr,"%g\n", tv.dval); }
                    else if (tv.type == VT_BOOL) { if (strcmp(op,"stdout")==0) printf("%s\n", tv.ival ? "true":"false"); else fprintf(stderr,"%s\n", tv.ival ? "true":"false"); }
                    else { if (strcmp(op,"stdout")==0) printf("%s\n", rest); else fprintf(stderr,"%s\n", rest); }
                }
            }
            ip++;
            continue;
        } else if (strcmp(op, "read")==0) {
            char varname[128];
            if (sscanf(p + strlen("read"), " %127s", varname) == 1) {
                trim(varname);
                Var *v = find_var(varname);
                if (!v) v = create_var(varname, VT_INT);
                // silent read: no printed prompt
                char buf[1024];
                if (!fgets(buf, sizeof(buf), stdin)) buf[0] = '\0';
                trim(buf);
                // type detection
                if (strcasecmp(buf,"true")==0 || strcasecmp(buf,"false")==0) {
                    v->type = VT_BOOL;
                    v->ival = (strcasecmp(buf,"true")==0) ? 1 : 0;
                } else {
                    // numeric detection
                    int isnum = 1, hasdot = 0;
                    char *q = buf;
                    if (*q=='+' || *q=='-') q++;
                    if (*q == '\0') isnum = 0;
                    while (*q) {
                        if (*q == '.') { if (hasdot) { isnum = 0; break;} hasdot = 1; }
                        else if (!isdigit((unsigned char)*q)) { isnum = 0; break; }
                        q++;
                    }
                    if (isnum) {
                        if (hasdot) { v->type = VT_DOUBLE; v->dval = atof(buf); }
                        else { v->type = VT_INT; v->ival = atoll(buf); }
                    } else {
                        v->type = VT_STR;
                        free(v->sval);
                        v->sval = strdup(buf);
                    }
                }
            }
            ip++;
            continue;
        } else if (strcmp(op, "return_code")==0) {
            char tok[256];
            if (sscanf(p + strlen("return_code"), " %255s", tok) == 1) {
                trim(tok);
                // store return into __ret variable
                Var *retv = find_var("__ret");
                if (!retv) retv = create_var("__ret", VT_INT);
                Var *valv = find_var(tok);
                if (valv) {
                    // copy
                    retv->type = valv->type;
                    retv->ival = valv->ival;
                    retv->dval = valv->dval;
                    free(retv->sval);
                    retv->sval = valv->sval ? strdup(valv->sval) : NULL;
                } else {
                    // literal?
                    set_var_from_literal(retv, tok);
                }
                // return: pop address and jump there
                int retaddr = pop_ret();
                if (retaddr == -1) {
                    // no return addr -> this is returning from main -> exit
                    return;
                } else {
                    ip = retaddr;
                    continue;
                }
            }
            ip++;
            continue;
        } else if (strcmp(op, "store")==0) {
            char typ[64], varname[128], value[512];
            if (sscanf(p + strlen("store"), " %63s %127s %511[^\n]", typ, varname, value) >= 2) {
                trim(value);
                // create/overwrite var
                Var *v = find_var(varname);
                if (!v) v = create_var(varname, VT_UNKNOWN);
                // set type based on typ if not unknown
                if (strcmp(typ,"int")==0) v->type = VT_INT;
                else if (strcmp(typ,"double")==0 || strcmp(typ,"float")==0) v->type = VT_DOUBLE;
                // value might be literal or token
                if (value[0] == '"' && value[strlen(value)-1] == '"') {
                    v->type = VT_STR;
                    free(v->sval);
                    v->sval = strdup(value+1);
                    v->sval[strlen(v->sval)-1] = '\0';
                } else if (find_var(value)) {
                    Var *src = find_var(value);
                    v->type = src->type;
                    v->ival = src->ival;
                    v->dval = src->dval;
                    free(v->sval);
                    v->sval = src->sval ? strdup(src->sval) : NULL;
                } else {
                    // literal
                    set_var_from_literal(v, value);
                }
            }
            ip++;
            continue;
        } else if (strcmp(op, "add")==0 || strcmp(op,"sub")==0 || strcmp(op,"mul")==0 ||
                   strcmp(op,"div")==0 || strcmp(op,"mod")==0 || strcmp(op,"pow")==0) {
            char a[256], b[256], dest[128];
            if (sscanf(p + strlen(op), " %255s %255s %127s", a, b, dest) >= 3) {
                trim(a); trim(b); trim(dest);
                exec_arith(op, a, b, dest);
            }
            ip++;
            continue;
        } else if (strcmp(op,"call")==0) {
            // call <func>(params)
            char rest[512];
            if (sscanf(p + strlen("call"), " %511[^\n]", rest) >= 1) {
                trim(rest);
                // extract name and params
                char fname[128];
                char params[512];
                char *par = strchr(rest, '(');
                if (par) {
                    int len = par - rest;
                    while (len>0 && isspace((unsigned char)rest[len-1])) len--;
                    strncpy(fname, rest, len); fname[len] = '\0';
                    char *pclose = strchr(par, ')');
                    params[0] = '\0';
                    if (pclose && pclose > par+1) {
                        int plen = pclose - par - 1;
                        strncpy(params, par+1, plen); params[plen] = '\0';
                    }
                } else {
                    strncpy(fname, rest, sizeof(fname)-1); fname[sizeof(fname)-1] = '\0';
                }
                trim(fname);
                int fidx = find_func(fname);
                if (fidx < 0) {
                    fprintf(stderr, "call to unknown function '%s'\n", fname);
                    ip++;
                    continue;
                }
                // Map params: split params and assign to function parameter names
                // Split params by comma
                char argtok[32][256];
                int argcnt = 0;
                if (params[0]) {
                    char tmp[512];
                    strncpy(tmp, params, sizeof(tmp)-1); tmp[sizeof(tmp)-1] = '\0';
                    char *t = strtok(tmp, ",");
                    while (t && argcnt < 32) {
                        trim(t);
                        strncpy(argtok[argcnt++], t, 255);
                        t = strtok(NULL, ",");
                    }
                }
                // For each param of function, assign value into variable with param name (shadowing global)
                Func *fn = &funcs[fidx];
                for (int pi=0; pi<fn->param_count; ++pi) {
                    const char *pname = fn->params[pi];
                    Var *pv = find_var(pname);
                    if (!pv) pv = create_var(pname, VT_UNKNOWN);
                    // if enough args provided, resolve arg tok
                    if (pi < argcnt) {
                        char *argstr = argtok[pi];
                        trim(argstr);
                        // if literal or variable
                        Var *src = find_var(argstr);
                        if (src) {
                            pv->type = src->type; pv->ival = src->ival; pv->dval = src->dval;
                            free(pv->sval); pv->sval = src->sval ? strdup(src->sval) : NULL;
                        } else {
                            set_var_from_literal(pv, argstr);
                        }
                    } else {
                        // missing arg -> set to 0
                        pv->type = VT_INT; pv->ival = 0;
                    }
                }
                // push return addr and jump to function body (line after entry)
                if (push_ret(ip+1) != 0) { fprintf(stderr,"callstack overflow\n"); return; }
                ip = funcs[fidx].line_index + 1;
                continue;
            }
            ip++;
            continue;
        } else {
            // unknown / unhandled
            ip++;
            continue;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s bytecode.fluxb\n", argv[0]);
        return 1;
    }
    load_lines(argv[1]);
    build_func_table();
    execute_bytecode();
    // free allocated strings and lines
    for (int i=0;i<nvars;i++) free(vars[i].sval);
    for (int i=0;i<nlines;i++) free(lines[i]);
    return 0;
}
