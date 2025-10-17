#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char *trim(char *s) {
    while(isspace(*s)) s++;
    if(*s==0) return s;
    char *end = s + strlen(s) - 1;
    while(end > s && isspace(*end)) end--;
    end[1] = '\0';
    return s;
}

int main(int argc, char *argv[]) {
    if(argc != 3) {
        printf("Usage: %s input.flux output.fluxb\n", argv[0]);
        return 1;
    }

    FILE *fin = fopen(argv[1], "r");
    FILE *fout = fopen(argv[2], "w");
    if(!fin || !fout) {
        printf("Error: Cannot open file\n");
        return 1;
    }

    char line[256];
    char current_func[64] = "";
    char current_type[16] = "";

    while(fgets(line, sizeof(line), fin)) {
        char *p = trim(line);
        if(strlen(p)==0) continue;

        // Function definition
        if(strstr(p, "(") && strstr(p, "):")) {
            char type[16], name[32], params[128];
            sscanf(p, "%s %[^:](%[^)]):", type, name, params);
            strcpy(current_func,name);
            strcpy(current_type,type);
            fprintf(fout,"entry %s %s(%s)\n", type, name, params);
            continue;
        }

        // variable declaration
        if(strncmp(p, "int ", 4)==0 || strncmp(p, "float ",6)==0 || strncmp(p,"double",6)==0 || strncmp(p,"bool",4)==0 || strncmp(p,"str",3)==0) {
            char type[16], var[32], val1[32], op[4], val2[32];
            int n = sscanf(p, "%s %s = %s %s %s", type, var, val1, op, val2);
            for(int i=0; var[i]; i++) if(var[i]=='='||var[i]==';') var[i]='\0';
            if(n==3) fprintf(fout,"store %s %s\n", var, val1);
            else if(n==5){
                char opStr[8];
                if(strcmp(op,"+")==0) strcpy(opStr,"add");
                else if(strcmp(op,"-")==0) strcpy(opStr,"sub");
                else if(strcmp(op,"*")==0) strcpy(opStr,"mul");
                else if(strcmp(op,"/")==0) strcpy(opStr,"div");
                fprintf(fout,"store %s %s %s %s\n", var, opStr, val1, val2);
            }
            continue;
        }

        // print
        if(strncmp(p,"print",5)==0) {
            char content[128], varlist[128];
            if(sscanf(p,"print(%[^)])",content)==1) {
                char *comma = strchr(content, ',');
                if(comma){
                    *comma='\0';
                    strcpy(varlist, comma+1);
                    fprintf(fout,"print_str %s %s\n", trim(content), trim(varlist));
                } else fprintf(fout,"print_str %s\n", trim(content));
            }
            continue;
        }

        // input
        if(strncmp(p,"input",5)==0) {
            char prompt[128], var[32];
            sscanf(p,"input(%[^,], %[^)])",prompt,var);
            fprintf(fout,"input %s %s\n", trim(prompt), trim(var));
            continue;
        }

        // return
        if(strncmp(p,"return",6)==0) {
            char ret[64];
            sscanf(p,"return %s",ret);
            fprintf(fout,"return_val %s\n",ret);
            continue;
        }

        // end
        if(strcmp(p,"end")==0){
            if(strlen(current_func)>0)
                fprintf(fout,"end %s()\n",current_func);
            else
                fprintf(fout,"halt\n");
            continue;
        }

        // function call
        if(strstr(p,"(") && strstr(p,")")){
            fprintf(fout,"call %s\n", p);
            continue;
        }
    }

    fclose(fin);
    fclose(fout);
    printf(" Compiled %s → %s\n", argv[1], argv[2]);
    return 0;
}
