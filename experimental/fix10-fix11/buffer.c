// LLL Buffer Plus — firmware CANONIQUE v1 "runtime-toggleable" (one-flash 128 Ko)
//
// Implémente le SPEC CANONIQUE v1 (2026-07-02, validé user) : le cœur autonome
// smart-buffer du standalone Mellow (porté mot-pour-mot, conventions halls/
// VACTUAL/IHOLD_IRUN identiques au variant déployé) + une couche de pilotage
// host Klipper complète : 11 fonctions runtime-toggleables, 4 niveaux
// d'automatisation pour FEED, registre IOEXT index-only, télémétrie events.
//
// Fichier AJOUTÉ (ne modifie aucun fichier Klipper existant) -> survit aux git pull.
// Hook Makefile requis : src-$(CONFIG_MACH_STM32F072) += buffer.c
//
// ---------------------------------------------------------------------------
// SÉCURITÉ (les 8 fixes du spec §1, tous présents) :
//  1. guard sched_is_shutdown() = LITTÉRALEMENT la 1re ligne de buffer_task
//  2. SINGLE-WRITER : les handlers de commandes ne touchent JAMAIS EN/UART —
//     flags uniquement ; seuls buffer_task/apply_state + buffer_shutdown écrivent
//  3. run FWD exige present && !is_error (vérifié à CHAQUE tick) ; BACK permis
//     sur runout (unload légitime)
//  4. back_timeout_ms (déf. 60 s) sur le gel-follow (parked) ET le run back
//  5. PB8/PB9 (headers 5 V) + PB14/PB15 (MDM host) HORS de la table IOEXT
//     (index-only => physiquement inatteignables)
//  6. tout STOP écrit VACTUAL=0 AVANT EN=1 ; toute mise en mouvement réécrit
//     GCONF+VACTUAL (apply_state) ; après un shutdown, tmc_ready=0 => resync
//  7. défauts compilés = role active / mode auto (north-star "continue de
//     buffer") ; le host rejoue rôle/mode/enables via add_config_cmd
//  8. AUCUN UART bit-bang dans les handlers ; les écritures TMC runtime sont
//     différées et espacées (> 1 ms, granularité de g_ms) dans la tâche
//  9. GRACE PERIOD BOOT (fix de la fenêtre « défauts compilés », finding
//     bench A9) : aucun mouvement autonome de l'étape 9 tant que le host n'a
//     JAMAIS parlé ET < 20 s depuis le boot — ferme la poussée fantôme d'un
//     reset MCU franc (FIRMWARE_RESTART / M112 / power-on). Compromis
//     standalone assumé : une carte jamais pilotée attend 20 s avant
//     l'auto-feed (le firmware Mellow d'origine démarre ~1 s après boot).
//     grace_done latché à chaque tick => jamais ré-armé (wrap g_ms inclus)
// 10. CONFIG TMC REECRITE AVANT CHAQUE MISE EN MOUVEMENT : le TMC2225 est alimente par le 24 V, la carte reste
//     en vie sur l'USB. Une coupure du 24 V avec l'USB branche remet le driver a ses reglages de mise sous tension
//     sans que le firmware le sache -> moteur lent, courant eleve, alim 24 V en limitation. La configuration
//     complete est donc reecrite avant chaque mise en mouvement ; EN n'est abaisse qu'apres (voir apply_state).
// 11. ARMEMENT ETALE : la reecriture du fix 10 (5 datagrammes, 60-70 ms d'attente active si faite d'un bloc) est
//     faite une ecriture par tour de buffer_task : plus aucun tour ne bloque la boucle (donc l'USB) plus de ~10 ms,
//     sous les 25 ms au-dela desquels l'hote Klipper retransmet -- sauf le tour de configuration du TMC (72 ms),
//     au boot et a chaque reprise apres shutdown. Meme sequence, meme ordre, EN abaisse apres la derniere ecriture.
//     Les ecritures differees (lll_param) passent APRES la decision d'etat du tour : un tour ecrit au plus UN
//     datagramme, STOP compris (le STOP reste immediat). Pendant un armement, l'etat garde par la FSM est le sens
//     vise, et un jog/run plus court que l'armement le termine (le moteur part puis s'arrete au tour suivant).
//     Consequences a connaitre : le moteur part ~60 ms apres la demande ; une demande des boutons / MBSIG, du role
//     parked ou du mode semi-manuel qui disparait avant la fin de l'armement (~50 ms) ne produit aucun mouvement.
//
// ---------------------------------------------------------------------------
// PROTOCOLE (bloc partagé firmware <-> klippy/extras/lll_buffer.py —
//            les formats doivent rester BYTE-IDENTIQUES des deux côtés) :
//
//  lll_role     role=%c            0=active 1=parked 2=standby
//  lll_set_idle value=%c           ALIAS back-compat (1->parked, 0->active)
//  lll_enable   func=%c en=%c      -> lll_ack func key=0xFE val=en ok
//  lll_mode     func=%c level=%c   (func 0 seul) 0=auto 1=semi-auto
//                                  2=semi-manuel 3=manuel -> ack key=0xFD
//  lll_param    func=%c key=%c val=%u -> "lll_ack func=%c key=%c val=%u ok=%c"
//  lll_arm                         semi-auto : arme UN cycle -> ack key=0xFB
//  lll_jog      dir=%c ms=%u       0=stop 1=fwd 2=back ; ms clampé 1..10000
//  lll_run      dir=%c ms=%u       borné ; ms=0 -> run_max_ms ; rafraîchissable
//  lll_stop                        stop moteur + clear jog/run/arm (flags only)
//  lll_clear_error                 refusé (ecode=5) si DIAG encore assert
//  lll_query                       -> "lll_state role=%c mode=%c error=%c
//                                      ecode=%c feeding=%c running=%c arm=%c
//                                      present=%c diag=%c cfgerr=%c"
//  lll_io_cfg   ch=%c func=%c arg=%u -> "lll_io_ack ch=%c func=%c ok=%c"
//  lll_io_write ch=%c val=%u
//  lll_io_read  ch=%c              -> "lll_io ch=%c val=%u ok=%c"
//  lll_unlock   key=%u             0xDEB1 : dégate les pins unverified (session)
//  PUSH asynchrone : "lll_event ev=%c a=%u"
//    ev: 0=ecode_set(a=ecode) 1=error_cleared 2=present_edge(a=0/1)
//        3=diag_edge(a=0/1) 4=button(a: 1=clic-retract 2=clic-feed
//        3=press-retract 4=press-feed 5=fin-maintien) 5=arm_change(a=bitfield)
//        6=role(a=role)
//
//  ENUMS : role   0=active 1=parked 2=standby
//          mode   0=auto 1=semi-auto 2=semi-manuel 3=manuel
//          ecode  0=none 1=feed_timeout 2=back_timeout 3=diag_fault
//                 4=host_alive 5=jam_cleared-pending
//          arm    bitfield brut H3H2H1 (bit2=hall3 bit1=hall2 bit0=hall1)
//          func   0=FEED 1=BUTTONS 2=MBSIG 3=ENTRANCE 4=TMC 5=MOVE 6=DIAG
//                 7=LED 8=ADC 9=TELEM 10=IOEXT
//
//  PARAMS (lll_param func key val) :
//   FEED(0):  0=feed_timeout_ms [1000..60000] déf 60000
//             1=back_timeout_ms [1000..60000] déf 60000
//   BUTTONS(1): 0=gate_by_role 0/1 déf 0 ; 1=inform_enable 0/1 déf 1 (PA2/PA3)
//   TMC(4):   0=IRUN [0..15]  (clamp = valeur déployée connue-bonne ; ne PAS
//                augmenter sans datasheet moteur — zéro pifomètre)
//             1=IHOLD [0..15] ; 2=IHOLDDELAY [0..15] ; 3=MRES [0..8]
//             (0=256µ..8=full ; NB: VACTUAL_MAG ne se rescale PAS tout seul)
//             4=stealth 0/1 (GCONF en_spreadcycle inversé)
//             5=standstill [0..3] (PWMCONF freewheel ; 1=freewheel exige IHOLD=0)
//             6=TPOWERDOWN [0..255] ; 7=vactual_mag [0..8388607] déf 77575
//   MOVE(5):  0=run_max_ms [1000..60000] déf 30000
//             1=host_alive_ms 0=off sinon [3000..60000] déf 0 (opt-in)
//   DIAG(6):  0=action 0=report-only(déf) 1=stop-latch
//             1=polarity 0=actif-haut(déf) 1=actif-bas   (bench !)
//   TELEM(9): 0=event_mask (bit par ev) déf 0x0F (error/present/diag)
//
//  IOEXT (index-only, ch -> pin compilé ; HORS TABLE = INATTEIGNABLE) :
//   ch0=PA4(IO1,adc) ch1=PA5(IO2,adc) ch2=PB10(IO4) ch3=PB11(IO3)
//   ch4=PA2(EXT1,adc,unv*) ch5=PA3(EXT2,adc,unv*) ch6=PB5(*MBSIG off)
//   ch7=PB6(*MBSIG off) ch8=PA8(LED1,unv) ch9=PA15(LED2,unv)
//   ch10=PA0(ADC-VIN,adc,unv) ch11=PA1(ADC-NTC,adc,unv)
//   funcs io_cfg : 0=release 1=din(arg=pull:0/1/2) 2=dout(arg: bit0=init,
//     bit1=shutdown_val, bits8..23=max_on s, 0=sans watchdog) 3=pwm-soft
//     (arg=période ms [10..10000], duty via io_write 0..255) 4=adc
//   unv = unverified -> exige lll_unlock ; conflits : ch4/5 exigent
//   BUTTONS-inform released ; ch6/7 MBSIG off ; ch8/9 LED off ; ch10/11 ADC off
//
// Conventions standalone reprises MOT POUR MOT (buffer.cpp / buffer.h) :
//   halls : obstrué = 1 ; PB7 : filament présent = 0 ; boutons : pressé = 0
//   HALL3 (PB4) obstrué -> Forward ; HALL2 (PB3) -> Stop ; HALL1 (PB2) -> Back
//   FORWARD = GCONF shaft=1 ; VACTUAL toujours POSITIF ; stop avant inversion
//
// This file may be distributed under the terms of the GNU GPLv3 license.

#include "autoconf.h"      // CONFIG_CLOCK_FREQ
#include "board/gpio.h"    // gpio_in_setup / gpio_out_setup / gpio_adc_*
#include "board/misc.h"    // timer_read_time
#include "sched.h"         // DECL_TASK / DECL_INIT / DECL_SHUTDOWN / sched_is_shutdown
#include "command.h"       // DECL_COMMAND / sendf

// ------------------------------------------------------------------
// Pins (encodage Klipper : GPIO(PORT,NUM) = (PORT-'A')*16 + NUM)  [buffer.h]
// ------------------------------------------------------------------
#define GPIO(PORT, NUM) (((PORT) - 'A') * 16 + (NUM))

#define HALL1_PIN     GPIO('B', 2)   // 光感3 -> pos3 -> Back
#define HALL2_PIN     GPIO('B', 3)   // 光感2 -> pos2 -> Stop
#define HALL3_PIN     GPIO('B', 4)   // 光感1 -> pos1 -> Forward
#define FIL_PIN       GPIO('B', 7)   // ENDSTOP_3 / 耗材开关 (filament switch)
#define KEY1_PIN      GPIO('B', 13)  // KEY1 后退 (retract)
#define KEY2_PIN      GPIO('B', 12)  // KEY2 前进 (feed)
#define FRONT_SIG_PIN GPIO('B', 5)   // MBSIG "avance" (actif bas) — OFF par défaut
#define BACK_SIG_PIN  GPIO('B', 6)   // MBSIG "recule" (actif bas) — OFF par défaut
#define EXT1_PIN      GPIO('A', 2)   // EXTENSION_PIN1 (inform feed, sortie)
#define EXT2_PIN      GPIO('A', 3)   // EXTENSION_PIN2 (inform retract, sortie)
#define EN_PIN        GPIO('A', 6)   // TMC enable (actif bas)
#define UART_PIN      GPIO('B', 1)   // TMC2208/2225 single-wire UART (PDN_UART)
#define DIAG_PIN      GPIO('B', 0)   // DIAG TMC (entrée) — polarité = bench
#define LED1_PIN      GPIO('A', 8)   // STATE_LED — bench
#define LED2_PIN      GPIO('A', 15)  // ERR_LED — bench

// ------------------------------------------------------------------
// Paramètres standalone (AUCUNE modif des valeurs déployées)
// ------------------------------------------------------------------
// VACTUAL = SPEED * microsteps * 200 / 60 / 0.715   (buffer.cpp)
#define SPEED_RPM        260
#define MICROSTEPS_DIV   64
#define VACTUAL_DEFAULT  ((int32_t)((int64_t)SPEED_RPM * MICROSTEPS_DIV * 200 / 60 * 1000 / 715))
#define VACTUAL_MAX      8388607   // borne HARDWARE : VACTUAL = 24 bits signés

#define TICKS_PER_MS     (CONFIG_CLOCK_FREQ / 1000)

// Bit-bang UART : 9600 bauds (standalone beginSerial(9600))
#define UART_BAUD        9600
#define BIT_TICKS        (CONFIG_CLOCK_FREQ / UART_BAUD)

// ------------------------------------------------------------------
// Registres TMC2208/2225 + valeurs de base (identiques au déployé)
// ------------------------------------------------------------------
#define TMC_GCONF        0x00
#define TMC_IHOLD_IRUN   0x10
#define TMC_TPOWERDOWN   0x11
#define TMC_VACTUAL      0x22
#define TMC_CHOPCONF     0x6C
#define TMC_PWMCONF      0x70
#define TMC_WRITE_FLAG   0x80
#define TMC_SLAVE_ADDR   0x00
#define TMC_SYNC         0x05

#define GCONF_BASE       0x000001C4UL   // spreadcycle,pdn_disable,mstep_reg,multistep_filt
#define GCONF_SHAFT      0x00000008UL   // bit3 shaft (FORWARD=1)
#define GCONF_SPREAD     0x00000004UL   // bit2 en_spreadcycle
#define CHOPCONF_BASE    0x12020055UL   // toff=5,vsense=1,mres=2(64µ),intpol=1
#define PWMCONF_BASE     0xC10D0024UL   // défaut TMC2208 (pwm_autoscale)
#define IRUN_DEFAULT     15             // rms_current(500 mA)@0.11 — connu-bon
#define IHOLD_DEFAULT    7
#define IHOLDDELAY_DEF   1
#define IRUN_MAX         15             // clamp = valeur déployée ; pas plus sans
                                        // datasheet moteur (zéro pifomètre)
#define TPOWERDOWN_DEF   20             // défaut reset TMC2208 (datasheet)

// ------------------------------------------------------------------
// Enums protocole
// ------------------------------------------------------------------
enum { ST_STOP = 0, ST_FORWARD, ST_BACK };
enum { ROLE_ACTIVE = 0, ROLE_PARKED, ROLE_STANDBY };
enum { FM_AUTO = 0, FM_SEMI_AUTO, FM_SEMI_MAN, FM_MANUAL };
enum { EC_NONE = 0, EC_FEED_TIMEOUT, EC_BACK_TIMEOUT, EC_DIAG_FAULT,
       EC_HOST_ALIVE, EC_CLEAR_PENDING };
enum { EV_ECODE = 0, EV_ERR_CLEAR, EV_PRESENT, EV_DIAG, EV_BUTTON,
       EV_ARM, EV_ROLE };
enum { FUNC_FEED = 0, FUNC_BUTTONS, FUNC_MBSIG, FUNC_ENTRANCE, FUNC_TMC,
       FUNC_MOVE, FUNC_DIAG, FUNC_LED, FUNC_ADC, FUNC_TELEM, FUNC_IOEXT,
       FUNC_COUNT };
#define FEN(f)           (1u << (f))
#define FEN_DEFAULT      (FEN(FUNC_FEED) | FEN(FUNC_BUTTONS) | FEN(FUNC_ENTRANCE) \
                          | FEN(FUNC_TMC) | FEN(FUNC_MOVE) | FEN(FUNC_TELEM))
#define UNLOCK_KEY       0xDEB1

// ------------------------------------------------------------------
// État global
// ------------------------------------------------------------------
static struct gpio_in hall1, hall2, hall3, fil, key1, key2, front_sig, back_sig,
                      diag_in;
static struct gpio_out en_out, uart_out, ext1, ext2, led1, led2;

static uint16_t func_en = FEN_DEFAULT;
static uint8_t role = ROLE_ACTIVE;
static uint8_t feed_mode = FM_AUTO;
static uint8_t armed;                  // semi-auto : un cycle armé
static uint8_t unlocked;               // lll_unlock session
static uint8_t cfgerr;                 // sticky : une config a été refusée

static uint8_t last_state = ST_STOP;   // état moteur APPLIQUÉ (source unique)
static uint8_t tmc_ready;
static uint8_t is_feeding;
static uint8_t is_error;
static uint8_t ecode = EC_NONE;

// intents host (posés par les handlers, appliqués par la tâche — single-writer)
static uint8_t host_jog;               // 0/1/2
static uint32_t jog_start_ms, jog_ms;
static uint8_t host_run;               // 0/1/2
static uint32_t run_start_ms, run_ms;
static uint8_t stop_req, clear_req;

// params runtime
static uint32_t feed_timeout_ms = 60000;
static uint32_t back_timeout_ms = 60000;
static uint32_t run_max_ms = 30000;
static uint32_t host_alive_ms;         // 0 = off (opt-in)
static uint8_t btn_gate_role;          // 0 = boutons actifs même parked (parité)
static uint8_t inform_en = 1;          // sorties PA2/PA3 inform
static uint8_t diag_action;            // 0=report-only 1=stop-latch
static uint8_t diag_pol;               // 0=actif-haut 1=actif-bas
static uint8_t telem_mask = 0x0F;

// shadows TMC + écritures différées (bit par registre)
static uint32_t gconf_shadow = GCONF_BASE;
static uint32_t chopconf_shadow = CHOPCONF_BASE;
static uint32_t pwmconf_shadow = PWMCONF_BASE;
static uint8_t tmc_irun = IRUN_DEFAULT, tmc_ihold = IHOLD_DEFAULT,
               tmc_iholddelay = IHOLDDELAY_DEF, tmc_tpowerdown = TPOWERDOWN_DEF;
static int32_t vactual_mag = VACTUAL_DEFAULT;
enum { TP_GCONF = 1, TP_CHOPCONF = 2, TP_IHOLD = 4, TP_PWMCONF = 8,
       TP_TPD = 16, TP_VACT = 32 };
static uint8_t tmc_pending;
static uint32_t tmc_last_ms;

// horloge ms (49 j de wrap ; le tick 32 bits @48 MHz wrappe à 89 s)
static uint32_t g_ms, ms_last_ticks, ms_acc;
#define ELAPSED_MS(since) ((uint32_t)(g_ms - (since)))
static void millis_update(void);

// armement étalé (fix #11) : état visé par l'armement en cours (ST_STOP = aucun),
// prochaine étape, et EN (actif bas) actuellement abaissé
static uint8_t arm_target = ST_STOP;
static uint8_t arm_step;
static uint8_t en_low;
enum { ARM_VACT0 = 0, ARM_CHOP, ARM_IHOLD, ARM_PWM, ARM_TPD, ARM_GCONF, ARM_GO };

// host-alive
static uint32_t last_host_ms;
static uint8_t alive_tripped;

// grace period boot (fix #9) : pas de feed AUTO avant 20 s OU 1ʳᵉ cmde host
static uint8_t host_ever;              // le host a parlé au moins une fois
static uint8_t grace_done;             // latch : grâce finie, jamais ré-armée
#define BOOT_GRACE_MS 20000

// boutons (polling non-bloquant, port des attachInterrupt du standalone)
static uint8_t key1_prev = 1, key2_prev = 1;
static uint32_t key1_press_ms, key2_press_ms, key1_rel_ms, key2_rel_ms;
static uint8_t key1_held, key2_held, key1_rel, key2_rel, key1_cnt, key2_cnt;
static uint8_t manual;                 // 0=aucun 1=fwd 2=back
static uint8_t inform;
static uint32_t inform_ms;

// gel-follow parked : borne back_timeout
static uint8_t parked_back_on;
static uint32_t parked_back_ms;

// edges télémétrie
static uint8_t prev_present = 0xFF, prev_arm = 0xFF;

// DIAG : débounce TEMPOREL (niveau stable >= 8 ms) — robuste quelle que soit
// la cadence des tâches (plancher 100 ms au repos)
static uint8_t diag_assert, diag_deb_lvl;
static uint32_t diag_stable_ms;

// LED : blink 2 Hz
#define LED_BLINK_MS 250

// ------------------------------------------------------------------
// IOEXT — registre index-only. Pins HORS TABLE = INATTEIGNABLES.
// HARD_RESERVED (jamais ici) : PA11/12 USB, PA13/14 SWD, PB1 UART, PA6 EN,
// PA7 DIR, PC13 STEP, PB2/3/4 halls, PB7, PB12/13 boutons, PB14/PB15 (MDM
// host), PB8/PB9 (headers 5 V), PF0/PF1 HSE.
// ------------------------------------------------------------------
#define IOEXT_NCH 12
enum { IOF_NONE = 0, IOF_DIN, IOF_DOUT, IOF_PWM, IOF_ADC };

struct ioext_def {
    uint8_t pin;
    uint8_t adc_ok;      // STM32F072 : ADC IN0-IN7 = PA0-PA7 seulement ici
    uint8_t unverified;  // exige lll_unlock
};
static const struct ioext_def ioext_def[IOEXT_NCH] = {
    { GPIO('A', 4),  1, 0 },   // ch0  IO1
    { GPIO('A', 5),  1, 0 },   // ch1  IO2
    { GPIO('B', 10), 0, 0 },   // ch2  IO4
    { GPIO('B', 11), 0, 0 },   // ch3  IO3
    { GPIO('A', 2),  1, 1 },   // ch4  EXT1 (net physique = bench)
    { GPIO('A', 3),  1, 1 },   // ch5  EXT2 (net physique = bench)
    { GPIO('B', 5),  0, 0 },   // ch6  MBSIG feed  (claim ssi MBSIG off)
    { GPIO('B', 6),  0, 0 },   // ch7  MBSIG retract
    { GPIO('A', 8),  0, 1 },   // ch8  LED1 (claim ssi LED off)
    { GPIO('A', 15), 0, 1 },   // ch9  LED2
    { GPIO('A', 0),  1, 1 },   // ch10 ADC VIN (claim ssi ADC off)
    { GPIO('A', 1),  1, 1 },   // ch11 ADC NTC
};

struct ioext_ch {
    uint8_t func;              // IOF_*
    uint8_t module_owned;      // claim interne du module ADC (func 8)
    struct gpio_in in;
    struct gpio_out out;
    struct gpio_adc adc;
    uint8_t out_level, shutdown_level;
    uint32_t max_on_ms;        // watchdog dout (0 = off)
    uint32_t on_since_ms;
    uint32_t pwm_period_ms, pwm_phase_ms;
    uint8_t pwm_duty;          // 0..255
    uint16_t adc_val;          // dernier échantillon brut 12 bits
};
static struct ioext_ch ioch[IOEXT_NCH];
static uint8_t adc_rr;         // round-robin échantillonnage
static uint8_t adc_inflight = 0xFF;

// ------------------------------------------------------------------
// UART TMC bit-bang TX (identique au déployé — TX-only, mode documenté)
// ------------------------------------------------------------------
static void
bit_delay(void)
{
    uint32_t start = timer_read_time();
    while (timer_read_time() - start < BIT_TICKS)
        ;
}

static void
uart_send_byte(uint8_t b)
{
    gpio_out_write(uart_out, 0);
    bit_delay();
    for (uint8_t i = 0; i < 8; i++) {
        gpio_out_write(uart_out, (b >> i) & 0x01);
        bit_delay();
    }
    gpio_out_write(uart_out, 1);
    bit_delay();
}

static uint8_t
tmc_crc(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        uint8_t cur = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if ((crc >> 7) ^ (cur & 0x01))
                crc = (crc << 1) ^ 0x07;
            else
                crc = crc << 1;
            cur >>= 1;
        }
    }
    return crc;
}

static void
tmc_write(uint8_t reg, uint32_t val)
{
    uint8_t dg[8];
    dg[0] = TMC_SYNC;
    dg[1] = TMC_SLAVE_ADDR;
    dg[2] = reg | TMC_WRITE_FLAG;
    dg[3] = (val >> 24) & 0xFF;
    dg[4] = (val >> 16) & 0xFF;
    dg[5] = (val >> 8) & 0xFF;
    dg[6] = val & 0xFF;
    dg[7] = tmc_crc(dg, 7);
    for (uint8_t i = 0; i < 8; i++)
        uart_send_byte(dg[i]);
    millis_update();                     // fix #11 : espacement mesuré depuis la fin du datagramme
    tmc_last_ms = g_ms;
}

static void
tmc_delay_ms(uint32_t ms)
{
    uint32_t start = timer_read_time();
    while (timer_read_time() - start < ms * TICKS_PER_MS)
        ;
}

// valeurs composées depuis les shadows
static uint32_t
tmc_ihold_irun_val(void)
{
    return (uint32_t)tmc_ihold | ((uint32_t)tmc_irun << 8)
           | ((uint32_t)tmc_iholddelay << 16);
}

static uint32_t
tmc_gconf_val(uint8_t state)
{
    return gconf_shadow | (state == ST_FORWARD ? GCONF_SHAFT : 0);
}

static void
tmc_configure(void)
{
    tmc_delay_ms(10);
    tmc_write(TMC_GCONF, gconf_shadow);          tmc_delay_ms(2);
    tmc_write(TMC_CHOPCONF, chopconf_shadow);    tmc_delay_ms(2);
    tmc_write(TMC_IHOLD_IRUN, tmc_ihold_irun_val()); tmc_delay_ms(2);
    tmc_write(TMC_PWMCONF, pwmconf_shadow);      tmc_delay_ms(2);
    tmc_write(TMC_TPOWERDOWN, tmc_tpowerdown);   tmc_delay_ms(2);
    tmc_write(TMC_VACTUAL, 0);                   tmc_delay_ms(2);
    last_state = ST_STOP;
    arm_target = ST_STOP;
    tmc_pending = 0;
}

// applique UNE écriture TMC différée par tick, espacée > 1 ms (fix #8 ; g_ms
// entier) ; appelée en FIN de tour, après la décision d'état (fix #11, voir buffer_task)
static void
tmc_apply_pending(void)
{
    if (!tmc_pending || ELAPSED_MS(tmc_last_ms) < 2)
        return;
    if (tmc_pending & TP_GCONF) {
        // fix #11 : moteur en marche -> son sens (le bit shaft ne bascule jamais
        // moteur tournant) ; arrêté pendant un armement -> le sens visé (sinon un
        // GCONF différé écrit entre deux étapes effacerait le sens demandé)
        tmc_write(TMC_GCONF, tmc_gconf_val(
            (arm_target != ST_STOP && last_state == ST_STOP) ? arm_target
                                                             : last_state));
        tmc_pending &= ~TP_GCONF;
    } else if (tmc_pending & TP_CHOPCONF) {
        tmc_write(TMC_CHOPCONF, chopconf_shadow);
        tmc_pending &= ~TP_CHOPCONF;
    } else if (tmc_pending & TP_IHOLD) {
        tmc_write(TMC_IHOLD_IRUN, tmc_ihold_irun_val());
        tmc_pending &= ~TP_IHOLD;
    } else if (tmc_pending & TP_PWMCONF) {
        tmc_write(TMC_PWMCONF, pwmconf_shadow);
        tmc_pending &= ~TP_PWMCONF;
    } else if (tmc_pending & TP_TPD) {
        tmc_write(TMC_TPOWERDOWN, tmc_tpowerdown);
        tmc_pending &= ~TP_TPD;
    } else if (tmc_pending & TP_VACT) {
        if (last_state != ST_STOP)      // refresh vitesse en mouvement
            tmc_write(TMC_VACTUAL, (uint32_t)vactual_mag);
        tmc_pending &= ~TP_VACT;
    }
}

// ------------------------------------------------------------------
// apply_state — SEUL écrivain du moteur avec tmc_apply_pending/configure
// (tous appelés exclusivement depuis buffer_task). Convention standalone :
// stop avant inversion, configuration réécrite à chaque mise en mouvement,
// VACTUAL=0 AVANT EN=1 sur STOP (fix #6).
//
// fix #10 : le TMC2225 est alimente par le 24 V, pas par l'USB. Une coupure du 24 V pendant que la carte reste
// alimentee par l'USB de l'hote remet ses registres de reset (GCONF mstep_reg_select=0 -> micro-pas des broches
// MS1/MS2, IHOLD_IRUN d'OTP) sans que ce firmware le sache : vitesse fausse, courant eleve, alim 24 V en limitation.
// Remede : la configuration COMPLETE est reecrite avant CHAQUE mise en mouvement, et EN n'est abaisse qu'ensuite
// (jamais un moteur en reglages d'usine au retour du 24 V).
//
// fix #11 : cette reecriture est ETALEE. buffer_task appelle apply_state a chaque tour avec l'etat
// voulu ; une mise en mouvement avance d'UNE ecriture par appel (VACTUAL=0 si inversion, CHOPCONF, IHOLD_IRUN,
// PWMCONF, TPOWERDOWN, GCONF, puis EN bas + VACTUAL), chacune a plus de 1 ms de la fin de la precedente (test
// ELAPSED_MS < 2 sur g_ms entier), et demande un
// tour immediat (sched_wake_tasks) tant qu'elle n'est pas finie. Le moteur part dans le meme delai qu'avant
// (~60 ms) mais la boucle principale (taches USB) tourne entre deux ecritures. STOP reste immediat et annule un
// armement en cours ; un changement de sens pendant l'armement le reprend du debut. Chaque etape ecrit la valeur
// courante de son registre et solde l'ecriture differee correspondante (comme le fix 10 apres sa sequence) ; un
// lll_param recu apres l'etape reste du et passe apres le depart.
// ------------------------------------------------------------------
static void
motor_stop_now(void)
{
    tmc_write(TMC_VACTUAL, 0);
    gpio_out_write(en_out, 1);
    en_low = 0;
}

static void
apply_state(uint8_t state)
{
    if (state == ST_FORWARD || state == ST_BACK) {
        if (state == last_state) {
            arm_target = ST_STOP;        // déjà en marche dans ce sens
            return;
        }
        if (arm_target != state) {       // nouvel armement (ou sens changé en cours d'armement)
            arm_target = state;
            arm_step = (last_state != ST_STOP) ? ARM_VACT0 : ARM_CHOP;
        }
        if (ELAPSED_MS(tmc_last_ms) < 2) {
            sched_wake_tasks();          // écriture trop récente : étape au tour suivant
            return;
        }
        switch (arm_step) {
        case ARM_VACT0:                  // stop avant inversion (EN reste bas)
            tmc_write(TMC_VACTUAL, 0);
            last_state = ST_STOP;
            arm_step = ARM_CHOP;
            break;
        case ARM_CHOP:
            tmc_write(TMC_CHOPCONF, chopconf_shadow);
            tmc_pending &= (uint8_t)~TP_CHOPCONF;
            arm_step = ARM_IHOLD;
            break;
        case ARM_IHOLD:
            tmc_write(TMC_IHOLD_IRUN, tmc_ihold_irun_val());
            tmc_pending &= (uint8_t)~TP_IHOLD;
            arm_step = ARM_PWM;
            break;
        case ARM_PWM:
            tmc_write(TMC_PWMCONF, pwmconf_shadow);
            tmc_pending &= (uint8_t)~TP_PWMCONF;
            arm_step = ARM_TPD;
            break;
        case ARM_TPD:
            tmc_write(TMC_TPOWERDOWN, tmc_tpowerdown);
            tmc_pending &= (uint8_t)~TP_TPD;
            arm_step = ARM_GCONF;
            break;
        case ARM_GCONF:
            tmc_write(TMC_GCONF, tmc_gconf_val(state));
            tmc_pending &= (uint8_t)~TP_GCONF;
            arm_step = ARM_GO;
            break;
        case ARM_GO:
        default:
            // VACTUAL = dernière vactual_mag reçue : un TP_VACT en attente est soldé ;
            // un autre lll_param reçu après son étape reste dû (tmc_apply_pending)
            gpio_out_write(en_out, 0);
            en_low = 1;
            tmc_write(TMC_VACTUAL, (uint32_t)vactual_mag);
            tmc_pending &= (uint8_t)~TP_VACT;
            last_state = state;
            arm_target = ST_STOP;
            // tour suivant immédiat : un jog/run échu pendant l'armement s'arrête
            // tout de suite (sinon au réveil périodique de 100 ms)
            sched_wake_tasks();
            return;
        }
        sched_wake_tasks();              // étape suivante sans attendre le réveil de 100 ms
        return;
    }
    // ST_STOP : immédiat, annule un armement en cours
    arm_target = ST_STOP;
    if (last_state == ST_STOP && !en_low)
        return;
    motor_stop_now();
    last_state = ST_STOP;
}

// ------------------------------------------------------------------
// Événements (TELEM)
// ------------------------------------------------------------------
static void
ev_send(uint8_t ev, uint32_t a)
{
    if (!(func_en & FEN(FUNC_TELEM)) || !(telem_mask & (1u << ev)))
        return;
    sendf("lll_event ev=%c a=%u", ev, a);
}

static void
set_ecode(uint8_t code, uint8_t latch_error)
{
    ecode = code;
    if (latch_error)
        is_error = 1;
    ev_send(EV_ECODE, code);
}

// ------------------------------------------------------------------
// Init (au boot) — cœur uniquement. Les pins des modules OFF par défaut
// (MBSIG PB5/PB6, DIAG PB0, LED PA8/PA15, ADC PA0/PA1) ne sont PAS réclamés
// ici : claim paresseux à l'enable (full-USB, pins non vérifiés intouchés).
// ------------------------------------------------------------------
void
buffer_init(void)
{
    hall1 = gpio_in_setup(HALL1_PIN, 0);
    hall2 = gpio_in_setup(HALL2_PIN, 0);
    hall3 = gpio_in_setup(HALL3_PIN, 0);
    fil   = gpio_in_setup(FIL_PIN, 0);
    key1  = gpio_in_setup(KEY1_PIN, 0);
    key2  = gpio_in_setup(KEY2_PIN, 0);
    en_out   = gpio_out_setup(EN_PIN, 1);        // moteur désactivé (actif bas)
    uart_out = gpio_out_setup(UART_PIN, 1);      // UART repos = haut
    // inform : sorties déployées/parité standalone (fw officiel : OUTPUT,
    // EXT1 init LOW / EXT2 init HIGH — confirmé RE binaire 2026-07-02)
    ext1 = gpio_out_setup(EXT1_PIN, 0);
    ext2 = gpio_out_setup(EXT2_PIN, 1);
}
DECL_INIT(buffer_init);

// ------------------------------------------------------------------
// IOEXT helpers
// ------------------------------------------------------------------
static void
ioext_release(uint8_t ch)
{
    if (ioch[ch].func == IOF_NONE)
        return;
    if (ioch[ch].func == IOF_ADC) {
        // conversion en vol : la consommer/annuler, sinon l'EOC jamais lu
        // gèle TOUS les autres canaux ADC (bug attrapé en revue)
        gpio_adc_cancel_sample(ioch[ch].adc);
        if (adc_inflight == ch)
            adc_inflight = 0xFF;
    }
    ioch[ch].func = IOF_NONE;
    ioch[ch].module_owned = 0;
    if ((ch == 4 || ch == 5) && (func_en & FEN(FUNC_BUTTONS)) && inform_en) {
        // rend PA2/PA3 aux sorties inform (sinon inform mort jusqu'au reboot)
        if (ch == 4)
            ext1 = gpio_out_setup(EXT1_PIN, 0);
        else
            ext2 = gpio_out_setup(EXT2_PIN, 1);
    } else {
        ioch[ch].in = gpio_in_setup(ioext_def[ch].pin, 0);   // Hi-Z
    }
}

static void
ioext_release_all(void)
{
    for (uint8_t i = 0; i < IOEXT_NCH; i++)
        if (!ioch[i].module_owned)       // les canaux du module ADC restent
            ioext_release(i);            // à lui (FUNC_ADC toujours enabled)
}

// moteur doux IOEXT : PWM soft + watchdog max_on des douts.
// NB : granularité du PWM soft = cadence des tâches (plancher 100 ms au
// repos) — adapté LED/relais/ventilo lent, PAS servo/signal précis.
static void
ioext_engine(void)
{
    for (uint8_t i = 0; i < IOEXT_NCH; i++) {
        struct ioext_ch *c = &ioch[i];
        if (c->func == IOF_PWM) {
            uint32_t pos = ELAPSED_MS(c->pwm_phase_ms);
            if (pos >= c->pwm_period_ms) {
                c->pwm_phase_ms = g_ms;
                pos = 0;
            }
            uint32_t on_ms = c->pwm_period_ms * c->pwm_duty / 256;
            gpio_out_write(c->out, pos < on_ms);
        } else if (c->func == IOF_DOUT && c->max_on_ms
                   && c->out_level != c->shutdown_level
                   && ELAPSED_MS(c->on_since_ms) > c->max_on_ms) {
            c->out_level = c->shutdown_level;    // watchdog : retombe
            gpio_out_write(c->out, c->out_level);
        }
    }
}

// échantillonnage ADC round-robin, non-bloquant (un canal en vol max)
static void
adc_engine(void)
{
    if (adc_inflight != 0xFF) {
        struct ioext_ch *c = &ioch[adc_inflight];
        if (c->func != IOF_ADC) {                // released entre-temps
            adc_inflight = 0xFF;
            return;
        }
        if (!gpio_adc_sample(c->adc)) {
            c->adc_val = gpio_adc_read(c->adc);
            adc_inflight = 0xFF;
        }
        return;
    }
    for (uint8_t n = 0; n < IOEXT_NCH; n++) {
        uint8_t i = (uint8_t)((adc_rr + 1 + n) % IOEXT_NCH);
        if (ioch[i].func == IOF_ADC) {
            adc_rr = i;
            adc_inflight = i;
            gpio_adc_sample(ioch[i].adc);        // lance la conversion
            return;
        }
    }
}

// claim interne module ADC (func 8) sur ch10/ch11
static void
adc_module_claim(void)
{
    for (uint8_t i = 10; i <= 11; i++) {
        ioch[i].adc = gpio_adc_setup(ioext_def[i].pin);
        ioch[i].func = IOF_ADC;
        ioch[i].module_owned = 1;
        ioch[i].adc_val = 0;
    }
}

static void
adc_module_release(void)
{
    for (uint8_t i = 10; i <= 11; i++)
        if (ioch[i].module_owned)
            ioext_release(i);
}

// ------------------------------------------------------------------
// DIAG (PB0) : débounce temporel (stable >= 8 ms), polarité paramétrable
// ------------------------------------------------------------------
static void
diag_engine(void)
{
    if (!(func_en & FEN(FUNC_DIAG)))
        return;
    uint8_t lvl = gpio_in_read(diag_in);
    uint8_t asserted = diag_pol ? !lvl : lvl;
    if (asserted != diag_deb_lvl) {
        diag_deb_lvl = asserted;         // changement : redémarre la fenêtre
        diag_stable_ms = g_ms;
        return;
    }
    if (ELAPSED_MS(diag_stable_ms) >= 8 && diag_assert != diag_deb_lvl) {
        diag_assert = diag_deb_lvl;
        ev_send(EV_DIAG, diag_assert);
        if (diag_assert && diag_action == 1)
            set_ecode(EC_DIAG_FAULT, 1);
        // la complétion d'un clear en attente (ecode 5) se fait dans la tâche
    }
}

// ------------------------------------------------------------------
// LED : PA8 état (présent=fixe, mouvement=blink, vide=off) ; PA15 erreur
// (fixe ; blink si clear-pending). Claim paresseux à l'enable.
// ------------------------------------------------------------------
static void
led_engine(void)
{
    if (!(func_en & FEN(FUNC_LED)))
        return;
    uint8_t blink = (g_ms / LED_BLINK_MS) & 1;
    uint8_t present = !gpio_in_read(fil);
    if (last_state != ST_STOP)
        gpio_out_write(led1, blink);
    else
        gpio_out_write(led1, present);
    if (is_error)
        gpio_out_write(led2, ecode == EC_CLEAR_PENDING ? blink : 1);
    else
        gpio_out_write(led2, 0);
}

// ------------------------------------------------------------------
// Boutons + MBSIG — port non-bloquant de motor_control (parité standalone).
// Retourne l'intent : 0=aucun, 1=fwd tenu, 2=back tenu, 3=fin de maintien.
// N'écrit JAMAIS le moteur (single-writer : la tâche applique).
// Gating séparé : clics/holds clavier = FUNC_BUTTONS ; PB5/PB6 = FUNC_MBSIG.
// NB cadence : les tâches Klipper tournent au réveil (plancher 100 ms au
// repos, bien plus vite quand la comm est active) — mêmes conditions que le
// déployé, clics/holds >= 100 ms fiables.
// ------------------------------------------------------------------
static uint8_t mb_only_hold;    // maintien courant piloté MBSIG seul (borné)
static uint8_t mb_tripped;      // borne mb-only déclenchée : signaux MBSIG
                                // ignorés jusqu'à désassertion des DEUX
static uint32_t hold_start_ms;

static uint8_t
process_buttons(void)
{
    uint8_t buttons_on = !!(func_en & FEN(FUNC_BUTTONS));
    uint8_t k1 = gpio_in_read(key1);    // 1=relâché, 0=pressé
    uint8_t k2 = gpio_in_read(key2);

    if (buttons_on) {
        if (key1_prev && !k1) { key1_press_ms = g_ms; key1_cnt++; key1_held = 1;
                                ev_send(EV_BUTTON, 3); }
        else if (!key1_prev && k1) {
            if (ELAPSED_MS(key1_press_ms) <= 500) { key1_rel = 1;
                                                    key1_rel_ms = g_ms; }
            else key1_cnt = 0;
            key1_held = 0;
        }
        key1_prev = k1;
        if (key2_prev && !k2) { key2_press_ms = g_ms; key2_cnt++; key2_held = 1;
                                ev_send(EV_BUTTON, 4); }
        else if (!key2_prev && k2) {
            if (ELAPSED_MS(key2_press_ms) <= 500) { key2_rel = 1;
                                                    key2_rel_ms = g_ms; }
            else key2_cnt = 0;
            key2_held = 0;
        }
        key2_prev = k2;

        // sorties inform : seulement si activées ET pins non réclamés IOEXT
        uint8_t inform_ok = inform_en && ioch[4].func == IOF_NONE
                            && ioch[5].func == IOF_NONE;
        if (inform && ELAPSED_MS(inform_ms) >= 3000) {
            inform = 0;
            if (inform_ok) { gpio_out_write(ext2, 1); gpio_out_write(ext1, 0); }
        }
        // clic court : inform + demande de clear (via le gate DIAG de la
        // tâche — un clic ne bypasse plus un stop-latch DIAG encore assert)
        if (key1_rel && ELAPSED_MS(key1_rel_ms) > 500) {
            key1_rel = 0;
            if (key1_cnt == 1) {
                if (inform_ok) { gpio_out_write(ext2, 0); }
                inform_ms = g_ms; inform = 1;
                if (!clear_req) clear_req = 2;   // 2 = source bouton
                ev_send(EV_BUTTON, 1);
            }
            key1_cnt = 0;
        }
        if (key2_rel && ELAPSED_MS(key2_rel_ms) > 500) {
            key2_rel = 0;
            if (key2_cnt == 1) {
                if (inform_ok) { gpio_out_write(ext1, 1); }
                inform_ms = g_ms; inform = 1;
                if (!clear_req) clear_req = 2;   // 2 = source bouton
                ev_send(EV_BUTTON, 2);
            }
            key2_cnt = 0;
        }
    } else {
        key1_prev = k1; key2_prev = k2;          // resync des fronts
        key1_held = key2_held = key1_rel = key2_rel = 0;
        key1_cnt = key2_cnt = 0;
    }

    // maintien long (>=500 ms) OU signal carte-mère bas (si MBSIG activé)
    uint8_t mb_back = 0, mb_fwd = 0;
    if (func_en & FEN(FUNC_MBSIG)) {
        mb_back = !gpio_in_read(back_sig);
        mb_fwd  = !gpio_in_read(front_sig);
        if (mb_tripped) {
            // borne déclenchée : on ignore MBSIG jusqu'à ce que les DEUX
            // signaux soient repassés inactifs (sinon un signal collé
            // relancerait une fenêtre fraîche à chaque trip)
            if (!mb_back && !mb_fwd)
                mb_tripped = 0;
            else
                mb_back = mb_fwd = 0;
        }
    }
    uint8_t key_back = buttons_on && key1_held
                       && ELAPSED_MS(key1_press_ms) >= 500;
    uint8_t key_fwd  = buttons_on && key2_held
                       && ELAPSED_MS(key2_press_ms) >= 500;

    if (key_back || mb_back) {                   // priorité BACK (standalone)
        uint8_t mo = !key_back;
        if (manual != 2) { manual = 2; hold_start_ms = g_ms; }
        else if (mo && !mb_only_hold)            // key->mb : fenêtre fraîche
            hold_start_ms = g_ms;
        mb_only_hold = mo;
        return 2;
    }
    if (key_fwd || mb_fwd) {
        uint8_t mo = !key_fwd;
        if (manual != 1) { manual = 1; hold_start_ms = g_ms; }
        else if (mo && !mb_only_hold)            // key->mb : fenêtre fraîche
            hold_start_ms = g_ms;
        mb_only_hold = mo;
        return 1;
    }
    if (manual)   { manual = 0; ev_send(EV_BUTTON, 5); return 3; }
    return 0;
}

// ------------------------------------------------------------------
// Handlers de commandes — FLAGS UNIQUEMENT (fix #2). Chaque commande
// rafraîchit le keepalive host-alive.
// ------------------------------------------------------------------
static void
ack(uint8_t func, uint8_t key, uint32_t val, uint8_t ok)
{
    if (!ok)
        cfgerr = 1;
    sendf("lll_ack func=%c key=%c val=%u ok=%c", func, key, val, ok);
}

static void
host_seen(void)
{
    last_host_ms = g_ms;
    alive_tripped = 0;
    host_ever = 1;                     // termine la grace period boot (fix #9)
}

void
command_lll_role(uint32_t *args)
{
    host_seen();
    uint8_t r = args[0];
    if (r > ROLE_STANDBY)
        return;
    if (r != role) {
        role = r;
        parked_back_on = 0;
        ev_send(EV_ROLE, role);
    }
}
DECL_COMMAND(command_lll_role, "lll_role role=%c");

// ALIAS back-compat : lll_set_idle 1 -> parked, 0 -> active
void
command_lll_set_idle(uint32_t *args)
{
    host_seen();
    uint8_t r = args[0] ? ROLE_PARKED : ROLE_ACTIVE;
    if (r != role) {
        role = r;
        parked_back_on = 0;
        ev_send(EV_ROLE, role);
    }
}
DECL_COMMAND(command_lll_set_idle, "lll_set_idle value=%c");

void
command_lll_enable(uint32_t *args)
{
    host_seen();
    uint8_t func = args[0], en = !!args[1];
    if (func >= FUNC_COUNT) {
        ack(func, 0xFE, en, 0);
        return;
    }
    // conflits IOEXT : refuse l'enable si les pins du module sont réclamés
    if (en) {
        if ((func == FUNC_MBSIG && (ioch[6].func || ioch[7].func))
            || (func == FUNC_LED && (ioch[8].func || ioch[9].func))
            || (func == FUNC_ADC && (ioch[10].func || ioch[11].func))) {
            ack(func, 0xFE, en, 0);
            return;
        }
    }
    uint8_t was = !!(func_en & FEN(func));
    if (was == en) {
        ack(func, 0xFE, en, 1);
        return;
    }
    if (en) {
        // claim paresseux des pins du module (GPIO config — pas moteur)
        if (func == FUNC_MBSIG) {
            front_sig = gpio_in_setup(FRONT_SIG_PIN, 1);   // pullup, actif bas
            back_sig  = gpio_in_setup(BACK_SIG_PIN, 1);
            mb_tripped = 0;
        } else if (func == FUNC_BUTTONS) {
            // restaure les sorties inform si laissées Hi-Z par un cycle
            // claim/release IOEXT pendant que BUTTONS était off
            if (inform_en && ioch[4].func == IOF_NONE
                && ioch[5].func == IOF_NONE) {
                ext1 = gpio_out_setup(EXT1_PIN, 0);
                ext2 = gpio_out_setup(EXT2_PIN, 1);
            }
        } else if (func == FUNC_DIAG) {
            diag_in = gpio_in_setup(DIAG_PIN, 0);
            diag_assert = 0; diag_deb_lvl = 0; diag_stable_ms = g_ms;
        } else if (func == FUNC_LED) {
            led1 = gpio_out_setup(LED1_PIN, 0);
            led2 = gpio_out_setup(LED2_PIN, 0);
        } else if (func == FUNC_ADC) {
            adc_module_claim();
        }
        func_en |= FEN(func);
    } else {
        func_en &= ~FEN(func);
        if (func == FUNC_MBSIG) {
            gpio_in_setup(FRONT_SIG_PIN, 0);               // relâche en Hi-Z
            gpio_in_setup(BACK_SIG_PIN, 0);
            if (manual) {                // hold MBSIG en vol -> STOP explicite
                manual = 0;              // (sinon héritage FSM state=last_state
                stop_req = 1;            //  = runaway BACK non borné)
            }
        } else if (func == FUNC_DIAG) {
            diag_assert = 0;
            gpio_in_setup(DIAG_PIN, 0);
        } else if (func == FUNC_LED) {
            gpio_out_write(led1, 0); gpio_out_write(led2, 0);
            gpio_in_setup(LED1_PIN, 0); gpio_in_setup(LED2_PIN, 0);
        } else if (func == FUNC_ADC) {
            adc_module_release();
        } else if (func == FUNC_IOEXT) {
            ioext_release_all();                           // sécurité
        } else if (func == FUNC_BUTTONS || func == FUNC_MBSIG) {
            if (manual) {                // un maintien en vol -> STOP explicite
                manual = 0;
                stop_req = 1;            // sinon le hold FSM hérite du mouvement
            }
        } else if (func == FUNC_MOVE) {
            if (host_jog || host_run) {  // tue le jog/run en vol
                host_jog = 0;
                host_run = 0;
                stop_req = 1;
            }
        } else if (func == FUNC_FEED) {
            is_feeding = 0;
        }
        // moteur : la tâche applique l'état qui découle au tick suivant
    }
    ack(func, 0xFE, en, 1);
}
DECL_COMMAND(command_lll_enable, "lll_enable func=%c en=%c");

void
command_lll_mode(uint32_t *args)
{
    host_seen();
    uint8_t func = args[0], level = args[1];
    if (func != FUNC_FEED || level > FM_MANUAL) {
        ack(func, 0xFD, level, 0);
        return;
    }
    feed_mode = level;
    armed = 0;
    ack(func, 0xFD, level, 1);
}
DECL_COMMAND(command_lll_mode, "lll_mode func=%c level=%c");

static uint32_t
clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void
command_lll_param(uint32_t *args)
{
    host_seen();
    uint8_t func = args[0], key = args[1];
    uint32_t val = args[2];
    uint32_t applied = val;              // l'ACK renvoie la valeur APPLIQUÉE
    uint8_t ok = 1;                      // (clampée), pas la valeur demandée
    switch (func) {
    case FUNC_FEED:
        if (key == 0)      applied = feed_timeout_ms = clamp_u32(val, 1000, 60000);
        else if (key == 1) applied = back_timeout_ms = clamp_u32(val, 1000, 60000);
        else ok = 0;
        break;
    case FUNC_BUTTONS:
        if (key == 0)      applied = btn_gate_role = !!val;
        else if (key == 1) {
            // refuse la ré-activation d'inform si PA2/PA3 réclamés par IOEXT
            if (val && (ioch[4].func || ioch[5].func)) { ok = 0; break; }
            applied = inform_en = !!val;
            if (inform_en) {             // restaure les sorties inform
                ext1 = gpio_out_setup(EXT1_PIN, 0);
                ext2 = gpio_out_setup(EXT2_PIN, 1);
            }
        }
        else ok = 0;
        break;
    case FUNC_TMC:
        if (!(func_en & FEN(FUNC_TMC))) { ok = 0; break; }
        switch (key) {
        case 0: applied = tmc_irun = clamp_u32(val, 0, IRUN_MAX);
                tmc_pending |= TP_IHOLD; break;
        case 1: applied = tmc_ihold = clamp_u32(val, 0, IRUN_MAX);
                tmc_pending |= TP_IHOLD; break;
        case 2: applied = tmc_iholddelay = clamp_u32(val, 0, 15);
                tmc_pending |= TP_IHOLD; break;
        case 3: if (val > 8) { ok = 0; break; }
                chopconf_shadow = (chopconf_shadow & ~(0xFUL << 24))
                                  | ((uint32_t)val << 24);
                tmc_pending |= TP_CHOPCONF; break;
        case 4: applied = !!val;
                gconf_shadow = val ? (gconf_shadow & ~GCONF_SPREAD)
                                   : (gconf_shadow | GCONF_SPREAD);
                tmc_pending |= TP_GCONF; break;
        case 5: if (val > 3) { ok = 0; break; }
                pwmconf_shadow = (pwmconf_shadow & ~(0x3UL << 20))
                                 | ((uint32_t)val << 20);
                tmc_pending |= TP_PWMCONF; break;
        case 6: applied = tmc_tpowerdown = clamp_u32(val, 0, 255);
                tmc_pending |= TP_TPD; break;
        case 7: applied = vactual_mag = clamp_u32(val, 0, VACTUAL_MAX);
                tmc_pending |= TP_VACT; break;
        default: ok = 0;
        }
        break;
    case FUNC_MOVE:
        if (key == 0)      applied = run_max_ms = clamp_u32(val, 1000, 60000);
        else if (key == 1) applied = host_alive_ms
                               = val ? clamp_u32(val, 3000, 60000) : 0;
        else ok = 0;
        break;
    case FUNC_DIAG:
        if (key == 0)      applied = diag_action = !!val;
        else if (key == 1) applied = diag_pol = !!val;
        else ok = 0;
        break;
    case FUNC_TELEM:
        if (key == 0)      applied = telem_mask = val & 0x7F;
        else ok = 0;
        break;
    default:
        ok = 0;
    }
    ack(func, key, ok ? applied : val, ok);
}
DECL_COMMAND(command_lll_param, "lll_param func=%c key=%c val=%u");

void
command_lll_arm(uint32_t *args)
{
    (void)args;
    host_seen();
    if (feed_mode == FM_SEMI_AUTO) {
        armed = 1;
        ack(FUNC_FEED, 0xFB, 1, 1);
    } else {
        ack(FUNC_FEED, 0xFB, 0, 0);
    }
}
DECL_COMMAND(command_lll_arm, "lll_arm");

void
command_lll_jog(uint32_t *args)
{
    host_seen();
    uint8_t dir = args[0];
    if (!dir || dir > 2) {
        // stop TOUJOURS accepté (même MOVE off) ; stop_req force le STOP :
        // sans lui, le hold FSM « aucun hall = garde l'état » hériterait du
        // mouvement host (bug attrapé en revue — régression vs déployé)
        host_jog = 0;
        stop_req = 1;
        return;
    }
    if (!(func_en & FEN(FUNC_MOVE))) {
        cfgerr = 1;
        return;
    }
    host_jog = dir;
    jog_ms = clamp_u32(args[1], 1, 10000);
    jog_start_ms = g_ms;
}
DECL_COMMAND(command_lll_jog, "lll_jog dir=%c ms=%u");

void
command_lll_run(uint32_t *args)
{
    host_seen();
    uint8_t dir = args[0];
    if (!dir || dir > 2) {
        host_run = 0;
        host_jog = 0;                    // parité déployé : run 0 coupe le jog
        is_feeding = 0;
        stop_req = 1;                    // idem jog : STOP explicite via tâche
        return;
    }
    if (!(func_en & FEN(FUNC_MOVE))) {
        cfgerr = 1;
        return;
    }
    host_run = dir;
    run_ms = args[1] ? clamp_u32(args[1], 1, run_max_ms) : run_max_ms;
    run_start_ms = g_ms;                 // rafraîchissable : re-send = re-fenêtre
}
DECL_COMMAND(command_lll_run, "lll_run dir=%c ms=%u");

void
command_lll_stop(uint32_t *args)
{
    (void)args;
    host_seen();
    stop_req = 1;                        // la tâche applique (single-writer)
    host_jog = 0;
    host_run = 0;
    armed = 0;
}
DECL_COMMAND(command_lll_stop, "lll_stop");

void
command_lll_clear_error(uint32_t *args)
{
    (void)args;
    host_seen();
    clear_req = 1;
}
DECL_COMMAND(command_lll_clear_error, "lll_clear_error");

void
command_lll_query(uint32_t *args)
{
    (void)args;
    host_seen();
    uint8_t arm = (gpio_in_read(hall3) << 2) | (gpio_in_read(hall2) << 1)
                  | gpio_in_read(hall1);
    sendf("lll_state role=%c mode=%c error=%c ecode=%c feeding=%c running=%c"
          " arm=%c present=%c diag=%c cfgerr=%c",
          role, feed_mode, is_error, ecode, is_feeding,
          last_state != ST_STOP, arm, !gpio_in_read(fil),
          diag_assert, cfgerr);
}
DECL_COMMAND(command_lll_query, "lll_query");

void
command_lll_io_cfg(uint32_t *args)
{
    host_seen();
    uint8_t ch = args[0], func = args[1];
    uint32_t arg = args[2];
    if (ch >= IOEXT_NCH || func > IOF_ADC || !(func_en & FEN(FUNC_IOEXT))) {
        cfgerr = 1;
        sendf("lll_io_ack ch=%c func=%c ok=%c", ch, func, 0);
        return;
    }
    struct ioext_ch *c = &ioch[ch];
    uint8_t ok = 1;
    if (c->module_owned) {
        ok = 0;             // canal possédé par le module ADC : passer par
                            // lll_enable func=8 en=0, pas par io_cfg
    } else if (func == IOF_NONE) {
        ioext_release(ch);
    } else if (ioext_def[ch].unverified && !unlocked) {
        ok = 0;                          // pin non vérifié : exige lll_unlock
    } else if ((ch == 4 || ch == 5)
               && (func_en & FEN(FUNC_BUTTONS)) && inform_en) {
        ok = 0;                          // PA2/PA3 : inform doit être released
    } else if ((ch == 6 || ch == 7) && (func_en & FEN(FUNC_MBSIG))) {
        ok = 0;
    } else if ((ch == 8 || ch == 9) && (func_en & FEN(FUNC_LED))) {
        ok = 0;
    } else if ((ch == 10 || ch == 11) && (func_en & FEN(FUNC_ADC))) {
        ok = 0;
    } else if (func == IOF_ADC && !ioext_def[ch].adc_ok) {
        ok = 0;                          // R_CAP_UNSUPPORTED (6/12 canaux ADC)
    } else {
        ioext_release(ch);               // libère l'ancienne config
        switch (func) {
        case IOF_DIN:
            // pull : 0=aucun, 1=up, 2=down (stm32 : pull_up 1 / -1)
            c->in = gpio_in_setup(ioext_def[ch].pin,
                                  arg == 1 ? 1 : (arg == 2 ? -1 : 0));
            break;
        case IOF_DOUT:
            c->out_level = arg & 1;
            c->shutdown_level = (arg >> 1) & 1;
            c->max_on_ms = ((arg >> 8) & 0xFFFF) * 1000;
            c->on_since_ms = g_ms;
            c->out = gpio_out_setup(ioext_def[ch].pin, c->out_level);
            break;
        case IOF_PWM:
            c->pwm_period_ms = clamp_u32(arg, 10, 10000);
            c->pwm_duty = 0;
            c->pwm_phase_ms = g_ms;
            c->shutdown_level = 0;
            c->out = gpio_out_setup(ioext_def[ch].pin, 0);
            break;
        case IOF_ADC:
            c->adc = gpio_adc_setup(ioext_def[ch].pin);
            c->adc_val = 0;
            break;
        }
        c->func = func;
        c->module_owned = 0;
    }
    if (!ok)
        cfgerr = 1;
    sendf("lll_io_ack ch=%c func=%c ok=%c", ch, func, ok);
}
DECL_COMMAND(command_lll_io_cfg, "lll_io_cfg ch=%c func=%c arg=%u");

void
command_lll_io_write(uint32_t *args)
{
    host_seen();
    uint8_t ch = args[0];
    uint32_t val = args[1];
    if (ch >= IOEXT_NCH)
        return;
    struct ioext_ch *c = &ioch[ch];
    if (c->func == IOF_DOUT) {
        c->out_level = !!val;
        c->on_since_ms = g_ms;
        gpio_out_write(c->out, c->out_level);
    } else if (c->func == IOF_PWM) {
        c->pwm_duty = val > 255 ? 255 : val;
    }
}
DECL_COMMAND(command_lll_io_write, "lll_io_write ch=%c val=%u");

void
command_lll_io_read(uint32_t *args)
{
    host_seen();
    uint8_t ch = args[0];
    uint32_t val = 0;
    uint8_t ok = 1;
    if (ch >= IOEXT_NCH) {
        ok = 0;
    } else {
        struct ioext_ch *c = &ioch[ch];
        switch (c->func) {
        case IOF_DIN:  val = gpio_in_read(c->in); break;
        case IOF_ADC:  val = c->adc_val; break;
        case IOF_DOUT: val = c->out_level; break;
        case IOF_PWM:  val = c->pwm_duty; break;
        default:       ok = 0;
        }
    }
    sendf("lll_io ch=%c val=%u ok=%c", ch, val, ok);
}
DECL_COMMAND(command_lll_io_read, "lll_io_read ch=%c");

void
command_lll_unlock(uint32_t *args)
{
    host_seen();
    unlocked = (args[0] == UNLOCK_KEY);
    ack(FUNC_IOEXT, 0xFC, unlocked, unlocked);
}
DECL_COMMAND(command_lll_unlock, "lll_unlock key=%u");

// ------------------------------------------------------------------
// Fail-safe shutdown : IRQ OFF -> UNIQUEMENT des gpio_out_write (pas d'UART).
// EN haut = driver désactivé ; douts IOEXT retombent à leur shutdown_val.
// ------------------------------------------------------------------
void
buffer_shutdown(void)
{
    gpio_out_write(en_out, 1);
    for (uint8_t i = 0; i < IOEXT_NCH; i++) {
        struct ioext_ch *c = &ioch[i];
        if (c->func == IOF_DOUT || c->func == IOF_PWM)
            gpio_out_write(c->out, c->shutdown_level);
    }
}
DECL_SHUTDOWN(buffer_shutdown);

// ------------------------------------------------------------------
// Horloge ms (wrap-safe : le tick 32 bits wrappe à ~89 s, g_ms à ~49 j)
// ------------------------------------------------------------------
static void
millis_update(void)
{
    uint32_t now = timer_read_time();
    ms_acc += now - ms_last_ticks;
    ms_last_ticks = now;
    if (ms_acc >= TICKS_PER_MS) {
        g_ms += ms_acc / TICKS_PER_MS;
        ms_acc %= TICKS_PER_MS;
    }
}

// suivi des fronts present/arm (télémétrie)
static void
track_edges(void)
{
    uint8_t present = !gpio_in_read(fil);
    if (present != prev_present) {
        if (prev_present != 0xFF)
            ev_send(EV_PRESENT, present);
        prev_present = present;
    }
    uint8_t arm = (gpio_in_read(hall3) << 2) | (gpio_in_read(hall2) << 1)
                  | gpio_in_read(hall1);
    if (arm != prev_arm) {
        if (prev_arm != 0xFF)
            ev_send(EV_ARM, arm);
        prev_arm = arm;
    }
}

// watchdog feed 60 s — TOUJOURS actif sur tout forward (auto ET commandé)
static uint32_t feed_start_ms_v;

static uint8_t
feed_watchdog(void)
{
    if (!is_feeding) {
        is_feeding = 1;
        feed_start_ms_v = g_ms;
        return 0;
    }
    if (ELAPSED_MS(feed_start_ms_v) > feed_timeout_ms) {
        set_ecode(EC_FEED_TIMEOUT, 1);
        is_feeding = 0;
        return 1;
    }
    return 0;
}

// ------------------------------------------------------------------
// TÂCHE — échelle de priorité UNIQUE du spec §4
// ------------------------------------------------------------------
static void
buffer_task_body(void)
{
    // 0. guard shutdown — LITTÉRALEMENT la première ligne (fix #1)
    if (sched_is_shutdown()) {
        gpio_out_write(en_out, 1);
        buffer_shutdown();               // douts IOEXT -> shutdown_val (idempotent)
        host_jog = 0; host_run = 0; is_feeding = 0; manual = 0; armed = 0;
        last_state = ST_STOP;
        arm_target = ST_STOP;            // fix #11 : armement abandonné, EN déjà haut
        en_low = 0;
        tmc_ready = 0;                   // resync TMC complet à la reprise (fix #6)
        return;
    }
    millis_update();

    // grace period boot (fix #9) : latch évalué à CHAQUE tick, même quand le
    // flot ne descend pas jusqu'à l'étape 9 (pas de filament, erreur, parked)
    // → le wrap de g_ms (49,7 j) ne peut JAMAIS ré-armer une grâce.
    if (!grace_done && (host_ever || g_ms >= BOOT_GRACE_MS))
        grace_done = 1;

    // 1. init TMC paresseuse (timer prêt au 1er tick, pas dans DECL_INIT)
    if (!tmc_ready) {
        tmc_configure();
        tmc_ready = 1;
        return;
    }

    track_edges();
    // (écritures TMC différées : en fin de tour, après la décision d'état — fix #11,
    // voir buffer_task)
    diag_engine();
    led_engine();
    adc_engine();
    ioext_engine();

    // clear d'erreur demandé (1=host, 2=clic bouton) : ne fait quelque chose
    // que s'il y a une erreur/ecode à effacer (un clic de swap sans erreur ne
    // spamme plus EV_ERR_CLEAR ni ne crée d'ecode=5 fantôme) ; refusé si DIAG
    // encore assert (-> ecode 5) ; cfgerr n'est purgé que par le HOST
    if (clear_req) {
        uint8_t from_host = (clear_req == 1);
        clear_req = 0;
        if (is_error || ecode != EC_NONE) {
            if (diag_assert && (func_en & FEN(FUNC_DIAG))) {
                ecode = EC_CLEAR_PENDING;    // en attente : DIAG doit retomber
            } else {
                is_error = 0;
                ecode = EC_NONE;
                parked_back_on = 0;          // fenêtre follow fraîche
                if (from_host)
                    cfgerr = 0;
                ev_send(EV_ERR_CLEAR, 0);
            }
        } else if (from_host) {
            cfgerr = 0;                      // clear host sans erreur : purge
        }
    }
    // complétion du clear en attente quand DIAG retombe
    if (ecode == EC_CLEAR_PENDING && !diag_assert) {
        is_error = 0;
        ecode = EC_NONE;
        parked_back_on = 0;
        ev_send(EV_ERR_CLEAR, 0);
    }

    // stop explicite (lll_stop) : stop immédiat, l'autonomie reprend au tick
    // suivant selon rôle/mode (north-star "continue de buffer")
    if (stop_req) {
        stop_req = 0;
        is_feeding = 0;
        apply_state(ST_STOP);
        return;
    }

    // 2. host-alive (opt-in) : expiré -> tue le mouvement HOST uniquement,
    // l'autonomie continue (north-star)
    if (host_alive_ms && ELAPSED_MS(last_host_ms) > host_alive_ms
        && !alive_tripped) {
        alive_tripped = 1;
        if (host_jog || host_run) {
            host_jog = 0;
            host_run = 0;
            is_feeding = 0;
            set_ecode(EC_HOST_ALIVE, 0);
            apply_state(ST_STOP);
            return;
        }
        set_ecode(EC_HOST_ALIVE, 0);
    }

    // 3. BOUTONS/MBSIG (parité standalone : marchent même parked, sauf gate)
    if ((func_en & (FEN(FUNC_BUTTONS) | FEN(FUNC_MBSIG)))
        && (!btn_gate_role || role == ROLE_ACTIVE)) {
        uint8_t bi = process_buttons();
        if (bi == 1 || bi == 2) {
            // sécurité plancher : un hold MBSIG SEUL (pas d'humain sur le
            // bouton) est borné feed/back_timeout (signal collé != user)
            if (mb_only_hold) {
                uint32_t lim = (bi == 1) ? feed_timeout_ms : back_timeout_ms;
                if (ELAPSED_MS(hold_start_ms) > lim) {
                    set_ecode(bi == 1 ? EC_FEED_TIMEOUT : EC_BACK_TIMEOUT, 1);
                    manual = 0;
                    mb_tripped = 1;      // ré-arme seulement à désassertion
                    apply_state(ST_STOP);
                    return;
                }
            }
            is_feeding = 0;              // gèle le chrono feed pendant un hold
            apply_state(bi == 1 ? ST_FORWARD : ST_BACK);
            return;
        }
        if (bi == 3) { apply_state(ST_STOP); return; }   // fin de maintien
    }

    // 4. JOG (override explicite, deadline <= 10 s)
    // fix #11 : un jog échu pendant son armement (~50 ms) finit l'armement : le
    // moteur part puis s'arrête au tour suivant, comme au fix 10 (un pas court ne
    // doit pas devenir « aucun mouvement »)
    if (host_jog) {
        uint8_t jdir = (host_jog == 1) ? ST_FORWARD : ST_BACK;
        if (ELAPSED_MS(jog_start_ms) < jog_ms || arm_target == jdir) {
            is_feeding = 0;              // gèle le chrono feed pendant un jog
            apply_state(jdir);
            return;
        }
        host_jog = 0;
        apply_state(ST_STOP);
        return;
    }

    // 5. RUN (borné ; FWD exige present && !error — vérifié CHAQUE tick fix #3)
    // fix #11 : même règle que le jog pour la fenêtre (pas pour les sécurités)
    if (host_run) {
        if (ELAPSED_MS(run_start_ms) >= run_ms
            && arm_target != (host_run == 1 ? ST_FORWARD : ST_BACK)) {
            host_run = 0;
            is_feeding = 0;
            apply_state(ST_STOP);
            return;
        }
        if (host_run == 1) {
            if (gpio_in_read(fil) || is_error) {         // runout ou erreur
                host_run = 0;
                is_feeding = 0;
                apply_state(ST_STOP);
                return;
            }
            // semi-manuel : le feed commandé s'auto-stoppe à MID/TAUT
            if (feed_mode == FM_SEMI_MAN
                && (gpio_in_read(hall2) || gpio_in_read(hall1))) {
                host_run = 0;
                is_feeding = 0;
                apply_state(ST_STOP);
                return;
            }
            if (feed_watchdog()) {                       // 60 s TOUJOURS (fix)
                host_run = 0;
                apply_state(ST_STOP);
                return;
            }
            apply_state(ST_FORWARD);
            return;
        }
        // BACK : permis sur runout (unload) mais borné back_timeout (fix #4)
        if (ELAPSED_MS(run_start_ms) > back_timeout_ms) {
            host_run = 0;
            set_ecode(EC_BACK_TIMEOUT, 1);
            apply_state(ST_STOP);
            return;
        }
        is_feeding = 0;
        apply_state(ST_BACK);
        return;
    }

    // 6. ENTRANCE : plus de filament -> STOP des chemins autos
    if ((func_en & FEN(FUNC_ENTRANCE)) && gpio_in_read(fil)) {
        apply_state(ST_STOP);
        is_feeding = 0;
        // parité standalone : retirer le filament efface un jam (feed OU
        // back timeout — les deux se résolvent physiquement en tirant le
        // filament) ; PAS un diag_fault (hardware) ni un clear-pending
        if (is_error && (ecode == EC_FEED_TIMEOUT || ecode == EC_BACK_TIMEOUT)) {
            is_error = 0;
            ecode = EC_NONE;
            ev_send(EV_ERR_CLEAR, 0);
        }
        return;
    }

    // 7. erreur latched -> STOP
    if (is_error) {
        apply_state(ST_STOP);
        is_feeding = 0;
        return;
    }

    // 8. RÔLE
    if (role == ROLE_STANDBY) {
        apply_state(ST_STOP);
        is_feeding = 0;
        return;
    }
    if (role == ROLE_PARKED) {
        // gel directionnel : suit un rewind (hall1) pour ne pas bloquer
        // l'unload, JAMAIS de forward ; borné back_timeout (fix #4)
        is_feeding = 0;
        if (gpio_in_read(hall1)) {
            if (!parked_back_on) {
                parked_back_on = 1;
                parked_back_ms = g_ms;
            }
            if (ELAPSED_MS(parked_back_ms) > back_timeout_ms) {
                set_ecode(EC_BACK_TIMEOUT, 1);
                parked_back_on = 0;      // fenêtre fraîche après clear
                apply_state(ST_STOP);
                return;
            }
            apply_state(ST_BACK);
        } else {
            parked_back_on = 0;
            apply_state(ST_STOP);
        }
        return;
    }

    // 9. FEED selon mode (role == ACTIVE) — machine à états halls standalone
    if (!(func_en & FEN(FUNC_FEED)) || feed_mode == FM_MANUAL) {
        apply_state(ST_STOP);
        is_feeding = 0;
        return;
    }
    // grace period boot (fix #9) : après un reset MCU franc, la tâche tourne
    // sur les défauts compilés (active/auto) avant l'arrivée de la config →
    // AUCUN mouvement autonome de l'étape 9 (feed forward ET back-follow du
    // rôle actif) tant que le host n'a JAMAIS parlé et < 20 s. En usage
    // Klipper, la 1ʳᵉ commande (config replay/poll) arrive en quelques
    // secondes et termine la grâce. Boutons, jog, run, rôles et follow
    // parked (étapes 3-8) = non affectés. Compromis standalone ASSUMÉ (pas
    // une parité) : une carte jamais pilotée attend 20 s au boot avant
    // l'auto-feed, là où le firmware Mellow d'origine démarre ~1 s après
    // boot — prix accepté pour fermer la poussée fantôme post-reset.
    if (!grace_done) {
        apply_state(ST_STOP);
        is_feeding = 0;
        return;
    }
    uint8_t h1 = gpio_in_read(hall1), h2 = gpio_in_read(hall2),
            h3 = gpio_in_read(hall3);
    uint8_t state;
    if (feed_mode == FM_SEMI_MAN) {
        // pas d'auto-start forward ; suivi tension-release conservé
        state = h1 ? ST_BACK : ST_STOP;
    } else {
        // AUTO / SEMI-AUTO : FSM standalone (aucun hall = garde l'état ; pendant
        // un armement, l'état gardé est le sens visé — fix #11 : last_state vaut
        // encore STOP et annulerait l'armement, là où le fix 10 tournait déjà)
        if (h3)      state = ST_FORWARD;
        else if (h2) state = ST_STOP;
        else if (h1) state = ST_BACK;
        else         state = (arm_target != ST_STOP) ? arm_target : last_state;
        if (feed_mode == FM_SEMI_AUTO) {
            // cycle consommé à MID seulement si un feed a réellement tourné
            if (h2 && last_state == ST_FORWARD)
                armed = 0;
            if (state == ST_FORWARD && !armed)
                state = ST_STOP;                 // pas armé : pas de feed
        }
    }
    if (state == ST_FORWARD) {
        if (feed_watchdog()) {
            apply_state(ST_STOP);
            return;
        }
    } else {
        is_feeding = 0;
    }
    apply_state(state);
}

void
buffer_task(void)
{
    uint8_t ready = tmc_ready;
    buffer_task_body();
    // fix #11 : écriture différée en FIN de tour, après la décision d'état. Si
    // apply_state vient d'écrire (étape d'armement, départ ou STOP), l'espacement
    // (> 1 ms) la reporte : un tour n'écrit jamais plus d'un datagramme (8,3 ms),
    // et un STOP ne suit plus une écriture différée dans le même tour (16,7 ms).
    // Rien en shutdown ni au tour de configuration (tmc_ready faux). Une écriture
    // reportée par l'espacement est reprise au tour suivant immédiat, pas au réveil
    // périodique de 100 ms.
    if (ready && tmc_ready) {
        tmc_apply_pending();
        if (tmc_pending)
            sched_wake_tasks();
    }
}
DECL_TASK(buffer_task);
