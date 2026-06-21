// Reproduz o padrao do Mario Kart: Raster em stream parado com 0x8000,
// e verifica que um comando seguinte (Target) NAO desincroniza.
// Com o bug antigo (atalho no WriteData), o LSB 0x00 do 0x8000 virava
// o comando 0x00 (Multiply) e corrompia tudo depois.

#include "sndsp1.h"
#include <cstdio>
#include <cstdint>

static void wbyte(SNDSP1 &d, uint8_t b){ d.WriteData(0,b); }
static void wword(SNDSP1 &d, int16_t w){ d.WriteData(0,(uint8_t)((uint16_t)w>>8)); d.WriteData(0,(uint8_t)((uint16_t)w&0xFF)); }
static int16_t rword(SNDSP1 &d){ uint8_t hi=d.ReadData(0), lo=d.ReadData(0); return (int16_t)(((uint16_t)hi<<8)|lo); }

static void doParameter(SNDSP1 &d){
    wbyte(d,0x02);
    wword(d,0);wword(d,0);wword(d,0);   // F
    wword(d,0x0600);wword(d,0x0200);    // Lfe,Les
    wword(d,0);wword(d,0x0800);         // Aas,Azs
    rword(d);rword(d);rword(d);rword(d);// Vof,Vva,Cx,Cy
}
static void doTarget(SNDSP1 &d, int16_t H, int16_t V, int16_t &X, int16_t &Y){
    wbyte(d,0x0E); wword(d,H); wword(d,V);
    X=rword(d); Y=rword(d);
}

int main(){
    SNDSP1 d;
    doParameter(d);

    // Target de referencia (sem nada antes)
    int16_t X1,Y1; doTarget(d,32,48,X1,Y1);
    printf("[ref ] Target(32,48) = (%d,%d)\n", X1,Y1);

    // ---- Raster em stream, parado com 0x8000 (padrao Mario Kart) ----
    wbyte(d,0x0A); wword(d,0x0000);     // opcode + Vs inicial
    for(int line=0; line<3; line++){    // le 3 scanlines completas (4 words cada)
        rword(d);rword(d);rword(d);rword(d);
    }
    // 4a "scanline": le An,Bn,Cn e ESCREVE 0x8000 no lugar do Dn -> para
    rword(d);rword(d);rword(d);
    wword(d,(int16_t)0x8000);

    // Target de novo: tem que bater com o de referencia
    int16_t X2,Y2; doTarget(d,32,48,X2,Y2);
    printf("[apos] Target(32,48) = (%d,%d)  %s\n", X2,Y2,
           (X2==X1 && Y2==Y1) ? "OK (sem desync!)" : "<-- DESYNC (bug)");

    // segundo round pra garantir que o estado continua limpo
    int16_t X3,Y3; doTarget(d,-32,48,X3,Y3);
    int16_t X4,Y4;
    doParameter(d); doTarget(d,-32,48,X4,Y4);
    printf("[ctrl] Target(-32,48)=(%d,%d) vs reParam (%d,%d) %s\n",
           X3,Y3,X4,Y4,(X3==X4&&Y3==Y4)?"OK":"(difere - reparametrizou)");
    return 0;
}
