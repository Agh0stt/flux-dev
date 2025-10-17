#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef enum {T_INT, T_STR} VarType;

typedef struct {
    char name[32];
    VarType type;
    union { int i; char str[128]; } value;
} Var;

Var vars[256];
int varCount = 0;

// --- Declarations ---
int getVarIndex(const char *name);
void setVarInt(const char *name,int val);
void setVarStr(const char *name,const char *val);
int getVarInt(const char *name);
char* getVarStr(const char *name);
char* trim(char *s);
void printString(const char *str,const char *varlist);

// --- Implementations ---
int getVarIndex(const char *name){
    for(int i=0;i<varCount;i++)
        if(strcmp(vars[i].name,name)==0) return i;
    return -1;
}

void setVarInt(const char *name,int val){
    int idx=getVarIndex(name);
    if(idx==-1){
        strcpy(vars[varCount].name,name);
        vars[varCount].type=T_INT;
        vars[varCount].value.i=val;
        varCount++;
    } else {
        vars[idx].type=T_INT;
        vars[idx].value.i=val;
    }
}

void setVarStr(const char *name,const char *val){
    int idx=getVarIndex(name);
    if(idx==-1){
        strcpy(vars[varCount].name,name);
        vars[varCount].type=T_STR;
        strcpy(vars[varCount].value.str,val);
        varCount++;
    } else {
        vars[idx].type=T_STR;
        strcpy(vars[idx].value.str,val);
    }
}

int getVarInt(const char *name){
    int idx=getVarIndex(name);
    if(idx!=-1 && vars[idx].type==T_INT) return vars[idx].value.i;
    fprintf(stderr,"Runtime error: unknown or non-int var '%s'\n",name);
    exit(1);
}

char* getVarStr(const char *name){
    int idx=getVarIndex(name);
    if(idx!=-1 && vars[idx].type==T_STR) return vars[idx].value.str;
    fprintf(stderr,"Runtime error: unknown or non-str var '%s'\n",name);
    exit(1);
}

char* trim(char *s){ 
    while(isspace(*s)) s++; 
    if(*s==0) return s; 
    char *e=s+strlen(s)-1; 
    while(e>s && isspace(*e)) e--; 
    e[1]='\0'; 
    return s;
}

void printString(const char *str,const char *varlist){
    char output[256];
    strcpy(output,str);
    int len=strlen(output);
    if(output[0]=='"' && output[len-1]=='"'){ output[len-1]='\0'; memmove(output,output+1,len-1);}
    if(varlist!=NULL){
        char copy[128]; strcpy(copy,varlist);
        char *tok=strtok(copy,",");
        while(tok){
            char key[32]; strcpy(key, trim(tok));
            int idx=getVarIndex(key);
            char val[128];
            if(idx!=-1){
                if(vars[idx].type==T_INT) sprintf(val,"%d",vars[idx].value.i);
                else strcpy(val,vars[idx].value.str);
            } else strcpy(val,"0");

            char *pos;
            while((pos=strstr(output,"$"))){
                pos++;
                if(strncmp(pos,key,strlen(key))==0){
                    char buffer[256];
                    *pos='\0';
                    sprintf(buffer,"%s%s%s",output,val,pos+strlen(key));
                    strcpy(output,buffer);
                } else break;
            }
            tok=strtok(NULL,",");
        }
    }
    printf("%s\n",output);
}

// --- Main ---
int main(int argc,char* argv[]){
    if(argc!=2){fprintf(stderr,"Usage: %s program.fluxb\n",argv[0]); return 1;}
    FILE* f=fopen(argv[1],"r");
    if(!f){fprintf(stderr,"Error: cannot open %s\n",argv[1]); return 1;}
    char line[256],op[32],arg1[128],arg2[128],arg3[128],arg4[128];

    while(fgets(line,sizeof(line),f)){
        char *p=line; while(*p && isspace(*p)) p++; if(*p==0||*p=='#') continue;
        int count=sscanf(p,"%s %s %s %s %s",op,arg1,arg2,arg3,arg4);

        if(strcmp(op,"store")==0){
            if(count==3){
                if(isdigit(arg2[0])||arg2[0]=='-') setVarInt(arg1,atoi(arg2));
                else setVarInt(arg1,getVarInt(arg2));
            }
            else if(count==5){
                int left=getVarInt(arg3);
                int right=getVarInt(arg4);
                if(strcmp(arg2,"add")==0) setVarInt(arg1,left+right);
                else if(strcmp(arg2,"sub")==0) setVarInt(arg1,left-right);
                else if(strcmp(arg2,"mul")==0) setVarInt(arg1,left*right);
                else if(strcmp(arg2,"div")==0) setVarInt(arg1,left/right);
            }
        } else if(strcmp(op,"print_str")==0){
            printString(arg1,count>2?arg2:NULL);
        } else if(strcmp(op,"input")==0){
            printf("%s",arg1); fflush(stdout);
            char input[128]; scanf("%s",input);
            if(isdigit(input[0])||input[0]=='-') setVarInt(arg2,atoi(input));
            else setVarStr(arg2,input);
        } else if(strcmp(op,"return_val")==0){
            if(isdigit(arg1[0])||arg1[0]=='-') setVarInt("__ret",atoi(arg1));
            else setVarStr("__ret",arg1);
        } else if(strcmp(op,"call")==0){
            // For now, we can just simulate calls , later we can add this 
        }
    }

    fclose(f);
    return 0;
}
