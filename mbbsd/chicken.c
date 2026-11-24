#include "bbs.h"
#include "daemons.h"   /* LOGIN_SRC_* (MtBBS-App ticket) */

#define NUM_KINDS   15		/* 有多少種動物 */
#define CHICKENLOG  "etc/chicken"
#define CHICKEN_PIC "etc/chickens"
#define FN_CHICKEN_LAST "chicken.last"  /* MilkTea: pet as owner last saw it */

static const char * const cage[17] = {
    "誕生", "週歲", "幼年", "少年", "青春", "青年",
    "青年", "活力", "壯年", "壯年", "壯年", "中年",
    "中年", "老年", "老年", "老摳摳", "古希"};
static const char * const chicken_type[NUM_KINDS] = {
    "小雞", "美少女", "勇士", "蜘蛛",
    "恐龍", "老鷹", "貓", "蠟筆小新",
    "狗狗", "惡魔", "忍者", "ㄚ扁",
    "馬英九", "就可人", "蘿莉"};
static const char * const chicken_food[NUM_KINDS] = {
    "飼料", "營養厚片", "雞排便當", "昆蟲",
    "肉", "肉", "貓餅乾", "小熊餅乾",
    "狗食", "靈氣", "飯團", "便當",
    "雞腿", "笑話文章", "水果沙拉"};
static const int egg_price[NUM_KINDS] = {
    5, 25, 30, 40,
    80, 50, 15, 35,
    17, 100, 85, 200,
    200, 100, 77};
static const int food_price[NUM_KINDS] = {
    4, 6, 8, 10,
    12, 12, 5, 6,
    5, 20, 15, 23,
    23, 10, 19};

enum {
    OO, FOOD, WEIGHT, DIRTY, BOOK, HAPPY, SATIS, TIREDSTRONG, SICK, HP_MAX,
    MAX_CHANGE
};

static const short time_change[NUM_KINDS][MAX_CHANGE] =
     /* 補品 食物 體重 髒亂 學問 快樂 滿意 疲勞 病氣 滿血 */
{
    /* 雞   */ {1, 1,   30,  3,  3, 40,  9,   7, 3,   30},
    /* 少女 */ {1, 1,  110,  1, 41, 20,  9,  25, 7,  110},
    /* 勇士 */ {1, 1,  200,  5, 33, 20, 15,  27, 1,  200},
    /* 蜘蛛 */ {1, 1,   10,  5,  1,  5,  3,   4, 1,   10},
    /* 恐龍 */ {1, 1, 1000,  9,  4, 12,  3, 200, 1, 1000},
    /* 老鷹 */ {1, 1,   90,  7,  4, 12,  3,  20, 5,   90},
    /* 貓   */ {1, 1,   30,  5,  4,  8,  3,   7, 4,   30},
    /* 小新 */ {1, 1,  100,  9, 20, 50, 10,  24, 4,  100},
    /* 狗   */ {1, 1,   45,  8,  3, 40, 20,   9, 5,   45},
    /* 惡魔 */ {1, 1,   45, 10,  5, 21, 11,   9, 5,   45},
    /* 忍者 */ {1, 1,   45,  2, 25,  1,  1,   9, 5,   45},
    /* 阿扁 */ {1, 1,  150,  4, 95, 25,  7,  25, 5,  175},
    /* 騜   */ {1, 1,  147,  2, 85, 20,  4,  25, 5,  145},
    /* 就可 */ {1, 1,  200,  3, 50, 50, 10,  10, 2,  300},
    /* 蘿莉 */ {1, 1,   80,  2,  2,  5,  7,  12, 1,  135},
};

/* ---- MilkTea: MtBBS-App 丟球票 ----------------------------------------
 * 只有 MtBBS-App 連線才有票；一般 telnet / gateway 完全不受影響。
 * 票存在使用者家目錄的 chicken.ticket（key=value），不動 chicken_t。
 * 目前只記錄，不發錢（記錄句尾標「未發放」）。
 */
#define FN_CHICKEN_TICKET   "chicken.ticket"
#define CHICKEN_DAILY_QUOTA 100     /* 每天「超過支出」的淨賺上限 */
#define TICKET_MAXVAL       100000  /* 單一次數的合理上限，防溢位 */

enum { TK_NONE = 0, TK_OPEN, TK_USED, TK_VOID };

typedef struct {
    char    id[8];          /* 6 碼票號；空字串＝沒有票 */
    int     date;           /* YYYYMMDD（伺服器時間） */
    int     quota_used;     /* 今天已用掉的 Quota（0..100） */
    int     visit_spent;    /* 這次進場在 App 連線裡花的錢 */
    int     state;          /* TK_* */
    long    start, endgame, issued;
    int     given;          /* 實給（目前不入帳） */
} ticket_t;

static ticket_t g_ticket;

static int
ticket_active(void)
{
    return g_ticket.id[0] && g_ticket.state == TK_OPEN;
}

static const char * const tk_state_name[] = { "none", "open", "used", "void" };

static void
ticket_save(void)
{
    char fn[PATHLEN], tmp[PATHLEN];
    FILE *fp;

    setuserfile(fn, FN_CHICKEN_TICKET);
    snprintf(tmp, sizeof(tmp), "%s.tmp", fn);
    fp = fopen(tmp, "w");
    if (!fp)
	return;
    fprintf(fp, "id=%s\ndate=%d\nquota_used=%d\nvisit_spent=%d\nstate=%s\n"
	    "start=%ld\nendgame=%ld\nissued=%ld\ngiven=%d\n",
	    g_ticket.id, g_ticket.date, g_ticket.quota_used,
	    g_ticket.visit_spent, tk_state_name[g_ticket.state],
	    g_ticket.start, g_ticket.endgame, g_ticket.issued, g_ticket.given);
    if (fclose(fp) == 0)
	rename(tmp, fn);
    else
	unlink(tmp);
}

/* 讀舊票只為了帶走「今天已用 Quota」；日期不同就歸零 */
static int
ticket_load_quota(int today)
{
    char fn[PATHLEN], line[128];
    FILE *fp;
    int date = 0, used = 0;

    setuserfile(fn, FN_CHICKEN_TICKET);
    fp = fopen(fn, "r");
    if (!fp)
	return 0;
    while (fgets(line, sizeof(line), fp)) {
	sscanf(line, "date=%d", &date);
	sscanf(line, "quota_used=%d", &used);
    }
    fclose(fp);
    if (date != today || used < 0)
	return 0;
    if (used > CHICKEN_DAILY_QUOTA)
	used = CHICKEN_DAILY_QUOTA;
    return used;
}

/* 進養雞場：只有 App 連線發新票 */
static void
ticket_issue(void)
{
    static const char alpha[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
    struct tm tm;
    time_t t = time(NULL);
    int i, today;

    memset(&g_ticket, 0, sizeof(g_ticket));
    if (mbbsd_login_source() != LOGIN_SRC_APP)
	return;

    localtime_r(&t, &tm);
    today = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
    srandom((unsigned int)(t ^ (getpid() << 8)));
    for (i = 0; i < 6; i++)
	g_ticket.id[i] = alpha[random() % (sizeof(alpha) - 1)];
    g_ticket.id[6] = 0;
    g_ticket.date = today;
    g_ticket.quota_used = ticket_load_quota(today);
    g_ticket.state = TK_OPEN;
    ticket_save();
}

static void
ticket_finish(int state)
{
    if (!g_ticket.id[0])
	return;
    if (g_ticket.state == TK_OPEN)
	g_ticket.state = state;
    ticket_save();
    g_ticket.id[0] = 0;     /* 離開後不再顯示、不再記支出 */
}

/* 只有 chicken.c 自己扣的錢才算「支出」；只算 App 連線 */
static int
ch_pay(int money, const char *item)
{
    int r = pay(money, "%s", item);
    if (money > 0 && r != -1 && ticket_active()) {
	g_ticket.visit_spent += money;
	ticket_save();
    }
    return r;
}

static void
ticket_duration(char *buf, size_t sz, long sec)
{
    if (sec < 0)
	sec = 0;
    if (sec >= 60)
	snprintf(buf, sz, "%ld 分 %ld 秒", sec / 60, sec % 60);
    else
	snprintf(buf, sz, "%ld 秒", sec);
}

/* 使用者離開後：停在空白輸入框等 App 回報。沒有動作就一直等。 */
static void
ticket_leave(const chicken_t *c)
{
    char buf[80], dur[40], rid[16], safe[16];
    int a, b2, cc, d, e, f, g, amt, calc, cap, give, n, i, j;

    if (!ticket_active())
	return;
    /* 這次沒花錢：App 不問丟球，票直接作廢 */
    if (g_ticket.visit_spent <= 0) {
	ticket_finish(TK_VOID);
	return;
    }

    g_ticket.start = (long)time(NULL);
    ticket_save();
    move(b_lines - 3, 0);
    clrtoeol();
    prints("票 %s：等待 MtBBS-App 回報丟球結果（不玩請直接按 Enter）", g_ticket.id);
    getdata(b_lines - 1, 0, "", buf, sizeof(buf), DOECHO);

    if (buf[0] == 0) {              /* 不玩 */
	ticket_finish(TK_VOID);
	return;
    }
    g_ticket.endgame = (long)time(NULL);
    if ((buf[0] == 'd' || buf[0] == 'D') && buf[1] == 0) {
	/* d + Enter：BBS 記下 endgame，接著等「票號 金額 a/b/c,d/e/f,g」 */
	getdata(b_lines - 1, 0, "", buf, sizeof(buf), DOECHO);
	if (buf[0] == 0) {
	    ticket_finish(TK_VOID);
	    return;
	}
    }
    g_ticket.issued = (long)time(NULL);

    n = sscanf(buf, "%15s %d %d/%d/%d,%d/%d/%d,%d",
	       rid, &amt, &a, &b2, &cc, &d, &e, &f, &g);
    if (n != 9 || amt < 0 || a < 0 || b2 < 0 || cc < 0 || d < 0 || e < 0 ||
	f < 0 || g < 0 || a > TICKET_MAXVAL || b2 > TICKET_MAXVAL ||
	cc > TICKET_MAXVAL || d > TICKET_MAXVAL || e > TICKET_MAXVAL ||
	f > TICKET_MAXVAL || g > TICKET_MAXVAL || amt > 100 * TICKET_MAXVAL) {
	log_filef(CHICKENLOG, LOG_CREAT,
		  "%s 的%s %s：回報格式不符，未發放\n",
		  cuser.userid, chicken_type[(int)c->type], c->name);
	ticket_finish(TK_USED);
	return;
    }

    if (strcasecmp(rid, g_ticket.id) != 0) {
	/* 只留英數字，避免把控制碼寫進記錄 */
	for (i = j = 0; rid[i] && j < 7; i++)
	    if (isalnum((unsigned char)rid[i]))
		safe[j++] = rid[i];
	safe[j] = 0;
	log_filef(CHICKENLOG, LOG_CREAT,
		  "%s 的%s %s：票號不符（收到 %s），未發放\n",
		  cuser.userid, chicken_type[(int)c->type], c->name, safe);
	ticket_finish(TK_USED);
	return;
    }

    calc = a * 1 + b2 * 3 + cc * 5 + d * 1 + e * 3 + f * 15 + g * 15;

    /* 回報 0 元、次數也都是 0：沒玩，不記 */
    if (amt == 0 && calc == 0) {
	ticket_finish(TK_VOID);
	return;
    }

    /* 金額跟次數對不上：整筆不給 */
    if (amt != calc) {
	log_filef(CHICKENLOG, LOG_CREAT,
		  "%s 的%s %s：回報 %d 元，與數據 (%d/%d/%d,%d/%d/%d,%d) 不符，次數算出 %d 元，未發放\n",
		  cuser.userid, chicken_type[(int)c->type], c->name,
		  amt, a, b2, cc, d, e, f, g, calc);
	g_ticket.given = 0;
	ticket_finish(TK_USED);
	return;
    }

    /* 本次最多可賺＝本次支出＋今天剩下的 Quota；超過的部分直接不給 */
    cap = g_ticket.visit_spent + (CHICKEN_DAILY_QUOTA - g_ticket.quota_used);
    give = calc > cap ? cap : calc;
    g_ticket.given = give;
    ticket_duration(dur, sizeof(dur), g_ticket.endgame - g_ticket.start);
    if (calc > cap)
	log_filef(CHICKENLOG, LOG_CREAT,
		  "%s 的%s %s 玩球 %s，等級次數算出 %d，數字正確，實給 %d (未發放)\n",
		  cuser.userid, chicken_type[(int)c->type], c->name,
		  dur, calc, give);
    else
	log_filef(CHICKENLOG, LOG_CREAT,
		  "%s 的%s %s 玩球 %s，得到 %d 元，其中直球 Excellent %d 個、曲球 Excellent %d 個 (未發放)\n",
		  cuser.userid, chicken_type[(int)c->type], c->name,
		  dur, give, cc, f);
    ticket_finish(TK_USED);
}
/* ---- end MtBBS-App 丟球票 ---- */

static void
show_chicken_picture(const char *fpath)
{
    // TODO change 5 to 4?
    show_file(fpath, 5, 14, SHOWFILE_ALLOW_ALL);
}

#ifdef HAVE_CHICKEN_CS
// External "common sense" module.
#include "chicken_cs.c"
#endif

static chicken_t*
load_live_chicken(const char *uid)
{
    char fn[PATHLEN];
    int fd = 0;
    chicken_t *p = NULL;

    if (!uid || !uid[0]) return NULL;
    sethomefile(fn, uid, FN_CHICKEN);
    if (!dashf(fn)) return NULL;
    fd = open(fn, O_RDWR);
    if (fd < 0) return NULL;

    // now fd is valid. open and mmap.
    p = mmap(NULL, sizeof(chicken_t), PROT_READ|PROT_WRITE, MAP_SHARED,
	    fd, 0);
    close(fd);
    return p;
}

static int
load_chicken(const char *uid, chicken_t *mychicken)
{
    char fn[PATHLEN];
    int fd = 0;

    memset(mychicken, 0, sizeof(chicken_t));
    if (!uid || !uid[0]) return 0;
    sethomefile(fn, uid, FN_CHICKEN);
    if (!dashf(fn)) return 0;
    fd = open(fn, O_RDONLY);
    if (fd < 0) return 0;
    if (read(fd, mychicken, sizeof(chicken_t)) > 0 && mychicken->name[0]) {
	close(fd);
	return 1;
    }
    close(fd);
    return 0;
}

static void
free_live_chicken(chicken_t *p)
{
    if (!p) return;
    munmap(p, sizeof(chicken_t));
}

/* MilkTea: remember the pet as the owner last saw it (called when leaving). */
static void
save_last_seen(const char *uid, const chicken_t *c)
{
    char fn[PATHLEN];
    int fd;

    if (!uid || !uid[0] || !c || !c->name[0]) return;
    sethomefile(fn, uid, FN_CHICKEN_LAST);
    fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return;
    if (write(fd, c, sizeof(chicken_t)) != (ssize_t)sizeof(chicken_t)) {
	// partial write: drop it so a broken snapshot is never restored
	close(fd);
	unlink(fn);
	return;
    }
    close(fd);
}

/* MilkTea: put the last-seen snapshot back. Returns 1 if restored. */
static int
restore_last_seen(const char *uid, chicken_t *cur)
{
    char fn[PATHLEN];
    chicken_t last;
    int fd, ok = 0;

    if (!uid || !uid[0] || !cur) return 0;
    sethomefile(fn, uid, FN_CHICKEN_LAST);
    fd = open(fn, O_RDONLY);
    if (fd < 0) return 0;
    // must be a living pet, and the same pet (same birth time) as the dead one
    if (read(fd, &last, sizeof(last)) == (ssize_t)sizeof(last) &&
	last.name[0] && last.cbirth == cur->cbirth)
	ok = 1;
    close(fd);
    if (!ok) return 0;

    last.lastvisit = now;	// no penalty for the time spent dead
    memcpy(cur, &last, sizeof(chicken_t));
    return 1;
}

static int
new_chicken(void)
{
    chicken_t mychicken;
    int  price, i;
    int fd;
    char fn[PATHLEN];
    const short *delta = NULL;

    memset(&mychicken, 0, sizeof(chicken_t));

    clear();
    move(2, 0);
    outs("歡迎光臨 " ANSI_COLOR(33) "◎" ANSI_COLOR(37;44) " "
	 BBSMNAME "寵物市場 " ANSI_COLOR(33;40) "◎" ANSI_RESET ".. "
	 "目前蛋價：\n"
	 "(a)小雞 $5   (b)美少女 $25  (c)勇士    $30  (d)蜘蛛  $40  "
	 "(e)恐龍 $80\n"
	 "(f)老鷹 $50  (g)貓     $15  (h)蠟筆小新$35  (i)狗狗  $17  "
	 "(j)惡魔 $100\n"
	 "(k)忍者 $85  (n)就可人$100  (m)蘿莉    $77\n"
	 "[0]不想買了 $0\n");
    i = vans("請選擇你要養的動物：");

    // since (o) is confusing to some people, we alias 'm' to 'o'.
    if (i == 'm') i = 'o';

    // (m, l) were political person.
    // do not make them in a BBS system...
    if (i == 'm' || i == 'l')
	return 0;

    i -= 'a';
    if (i < 0 || i > NUM_KINDS - 1)
	return 0;

    mychicken.type = i;

    price = egg_price[(int)mychicken.type];
    reload_money();
    if (cuser.money < price) {
	vmsgf(MONEYNAME "不夠買蛋(要 %d)", price);
	return 0;
    }

    while (strlen(mychicken.name) < 3)
    {
	getdata(8, 0, "幫牠取個好名字：", mychicken.name,
		sizeof(mychicken.name), DOECHO);
    }

    delta = time_change[(int)mychicken.type];
    mychicken.lastvisit = mychicken.birthday = mychicken.cbirth = now;
    mychicken.weight = delta[WEIGHT] / 3;
    mychicken.book = delta[BOOK];
    mychicken.happy = delta[HAPPY];
    mychicken.satis = delta[SATIS];
    mychicken.hp = delta[WEIGHT];
    mychicken.hp_max = delta[WEIGHT];

    reload_money();
    if (cuser.money < price)
    {
	vmsg("錢不夠了。");
	return 0;
    }
    ch_pay(price, "寵物蛋");

    // flush it
    setuserfile(fn, FN_CHICKEN);
    fd = OpenCreate(fn, O_WRONLY);
    if (fd < 0)
    {
	vmsg("系統錯誤: 無法建立資料，請至 " BN_BUGREPORT " 報告。");
	return 0;
    }

    write(fd, &mychicken, sizeof(chicken_t));
    close(fd);

    // log data
    log_filef(CHICKENLOG, LOG_CREAT,
              ANSI_COLOR(31) "%s " ANSI_RESET
              "養了一隻叫" ANSI_COLOR(33) " %s " ANSI_RESET "的 "
              ANSI_COLOR(32) "%s" ANSI_RESET "  於 %s\n", cuser.userid,
              mychicken.name, chicken_type[(int)mychicken.type], Cdate(&now));
    return 1;
}

static void
show_chicken_stat(const chicken_t * thechicken, int age)
{
    struct tm ptime;
    char hp_buf[STRLEN];
    char tkbuf[64] = "";
    int tklen = 0, pad;

    // line 1: status
    // line 2: buff
    // line 3: debuff

    localtime4_r(&thechicken->birthday, &ptime);
    // MilkTea: MtBBS-App connections see the ticket id after the pet type
    if (ticket_active()) {
	snprintf(tkbuf, sizeof(tkbuf), " " ANSI_COLOR(36) "%s" ANSI_RESET, g_ticket.id);
	tklen = 1 + (int)strlen(g_ticket.id);
    }
    pad = (int)(30 - strlen(thechicken->name) -
		strlen(chicken_type[(int)thechicken->type]) - tklen);
    if (tklen && pad < 1)
	pad = 1;
    prints("名字: " ANSI_COLOR(33) "%s" ANSI_RESET
           " (" ANSI_COLOR(32) "%s" ANSI_RESET ")%s%*s"
           "生日:" ANSI_COLOR(33) "%d" ANSI_RESET "年"
           ANSI_COLOR(33) "%d" ANSI_RESET "月"
           ANSI_COLOR(33) "%d" ANSI_RESET "日 "
	   "(" ANSI_COLOR(32) "%s %d歲" ANSI_RESET ")\n",
	   thechicken->name, chicken_type[(int)thechicken->type],
           tkbuf, pad,
           "", ptime.tm_year + 1900, ptime.tm_mon + 1, ptime.tm_mday,
	   cage[age > 16 ? 16 : age], age);

    snprintf(hp_buf, sizeof(hp_buf), "%d / %d", thechicken->hp,
             thechicken->hp_max);

    prints( "體力:" ANSI_COLOR(33) " %-19s" ANSI_RESET
           " 體重:" ANSI_COLOR(33) "%-7.1f" ANSI_RESET
           " 食物:" ANSI_COLOR(36) "%-7d" ANSI_RESET
           " 藥品:" ANSI_COLOR(36) "%-7d" ANSI_RESET
           " 補品:" ANSI_COLOR(36) "%-7d" ANSI_RESET
           "\n",
           hp_buf,
	   ((float)(thechicken->hp_max + (thechicken->weight / 50))) / 100,
           thechicken->food,
           thechicken->medicine,
	   thechicken->oo);

    prints( "快樂:" ANSI_COLOR(32) "%-7d" ANSI_RESET
           " 滿意:" ANSI_COLOR(32) "%-7d" ANSI_RESET
           " 學問:" ANSI_COLOR(32) "%-7d" ANSI_RESET
           " 髒亂:" ANSI_COLOR(31) "%-7d" ANSI_RESET
           " 生病:" ANSI_COLOR(31) "%-7d" ANSI_RESET
           " 疲勞:" ANSI_COLOR(31) "%-7d" ANSI_RESET
           "\n",
	   thechicken->happy,
           thechicken->satis,
           thechicken->book,
           thechicken->dirty,
	   thechicken->sick,
           thechicken->tired);
}

static void
show_chicken_data(chicken_t * thechicken)
{
    char            buf[1024];
    int age = ((now - thechicken->cbirth) / (60 * 60 * 24));
    if (age < 0) {
	thechicken->birthday = thechicken->cbirth = now - 10 * (60 * 60 * 24);
	age = 10;
    }

    /* Ptt:debug */
    thechicken->type %= NUM_KINDS;
    clear();
    showtitle(BBSMNAME2 "養雞場", BBSName);
    move(1, 0);

    show_chicken_stat(thechicken, age);
    snprintf(buf, sizeof(buf), CHICKEN_PIC "/%c%d", thechicken->type + 'a',
	     age > 16 ? 16 : age);
    show_chicken_picture(buf);
    move(18, 0);

    // status line - try harder to not exceed column 80.

    if (thechicken->sick > thechicken->hp / 5)
	outs(ANSI_COLOR(5;31) "病重!" ANSI_RESET);
    else if (thechicken->sick)
	outs("生病了.");

    if (thechicken->dirty > 150)
	outs(ANSI_COLOR(31) "又臭又髒." ANSI_RESET);
    else if (thechicken->dirty > 80)
	outs("有點髒.");
    else if (thechicken->dirty < 20)
	outs(ANSI_COLOR(32) "很乾淨." ANSI_RESET);

    if (thechicken->weight > thechicken->hp_max * 4)
	outs(ANSI_COLOR(31) "快撐死了." ANSI_RESET);
    else if (thechicken->weight > thechicken->hp_max * 3)
	outs(ANSI_COLOR(32) "飽嘟嘟." ANSI_RESET);
    else if (thechicken->weight < (thechicken->hp_max / 4))
	outs(ANSI_COLOR(31) "快餓死了." ANSI_RESET);
    else if (thechicken->weight < (thechicken->hp_max / 2))
	outs("餓了.");

    if (thechicken->tired > thechicken->hp * 1.7)
	outs(ANSI_COLOR(31) "累到昏迷." ANSI_RESET);
    else if (thechicken->tired > thechicken->hp)
	outs("累了.");
    else if (thechicken->tired < thechicken->hp / 4)
	outs(ANSI_COLOR(32) "精力旺盛." ANSI_RESET);

    if (thechicken->hp < thechicken->hp_max / 4)
	outs(ANSI_COLOR(31) "體力用盡." ANSI_RESET);

    if (thechicken->happy > 500)
	outs(ANSI_COLOR(32) "很快樂." ANSI_RESET);
    else if (thechicken->happy < 100)
	outs("不快樂.");

    if (thechicken->satis > 500)
	outs(ANSI_COLOR(32) "很滿足." ANSI_RESET);
    else if (thechicken->satis < 50)
	outs("不滿足.");
}

// Actions

static void
ch_eat(chicken_t *mychicken)
{
    if (mychicken->food) {
	mychicken->weight += time_change[(int)mychicken->type][WEIGHT] +
	mychicken->hp_max / 5;
	mychicken->tired +=
	    time_change[(int)mychicken->type][TIREDSTRONG] / 2;
	mychicken->hp_max++;
	mychicken->happy += 5;
	mychicken->satis += 7;
	mychicken->food--;
	move(10, 10);

	show_chicken_picture(CHICKEN_PIC "/eat");
	pressanykey();
    }
}

static void
ch_clean(chicken_t *mychicken)
{
    mychicken->dirty = 0;
    mychicken->tired +=
	time_change[(int)mychicken->type][TIREDSTRONG] / 3;
    show_chicken_picture(CHICKEN_PIC "/clean");
    pressanykey();
}

static void
ch_guess(chicken_t *mychicken)
{
    char           *guess[3] = {"剪刀", "石頭", "布"}, me, ch, win;

    mychicken->happy += time_change[(int)mychicken->type][HAPPY] * 1.5;
    mychicken->satis += time_change[(int)mychicken->type][SATIS];
    mychicken->tired += time_change[(int)mychicken->type][TIREDSTRONG];
    move(20, 0);
    clrtobot();
    outs("你要出[" ANSI_COLOR(32) "1" ANSI_RESET "]" ANSI_COLOR(33) "剪刀"
         ANSI_RESET "(" ANSI_COLOR(32) "2" ANSI_RESET ")"
	 ANSI_COLOR(33) "石頭" ANSI_RESET "(" ANSI_COLOR(32) "3" ANSI_RESET ")"
         ANSI_COLOR(33) "布" ANSI_RESET ":\n");
    me = vkey();
    me -= '1';
    if (me > 2 || me < 0)
	me = 0;
    win = (int)(3.0 * random() / (RAND_MAX + 1.0)) - 1;
    ch = (me + win + 3) % 3;
    prints("%s:%s !      %s:%s !.....%s",
	   cuser.userid, guess[(int)me], mychicken->name, guess[(int)ch],
	   win == 0 ? "平手" : win < 0 ? "耶..贏了 :D!!" : "嗚..我輸了 :~");
    pressanykey();
}

static void
ch_book(chicken_t *mychicken)
{
    mychicken->book += time_change[(int)mychicken->type][BOOK];
    mychicken->tired += time_change[(int)mychicken->type][TIREDSTRONG];
    show_chicken_picture(CHICKEN_PIC "/read");
    pressanykey();
}

static void
ch_kiss(chicken_t *mychicken)
{
    mychicken->happy += time_change[(int)mychicken->type][HAPPY];
    mychicken->satis += time_change[(int)mychicken->type][SATIS];
    mychicken->tired +=
	time_change[(int)mychicken->type][TIREDSTRONG] / 2;
    show_chicken_picture(CHICKEN_PIC "/kiss");
    pressanykey();
}

static void
ch_hit(chicken_t *mychicken)
{
    mychicken->weight -= mychicken->hp_max / 15;
    mychicken->hp -= (int)((float)time_change[(int)mychicken->type][HP_MAX] *
			   random() / (RAND_MAX + 1.0)) / 2 + 1;

    if (mychicken->book > 2)
	mychicken->book -= 2;
    if (mychicken->happy > 2)
	mychicken->happy -= 2;
    if (mychicken->satis > 2)
	mychicken->satis -= 2;
    mychicken->tired += time_change[(int)mychicken->type][TIREDSTRONG];
    show_chicken_picture(CHICKEN_PIC "/hit");
    pressanykey();
}

static void
ch_buyitem(int money, const char *picture, int *item, chicken_t *mychicken GCC_UNUSED)
{
    int num = 0;
    char buf[5];
    char prompt[STRLEN];

#ifdef HAVE_CHICKEN_CS
    if (ch_buyitem_cs(money, item, mychicken))
        return;
#endif
    snprintf(prompt, sizeof(prompt),
             "單價 $%d " MONEYNAME "，要買多少份呢: ", money);

    getdata_str(b_lines - 1, 0, prompt, buf, sizeof(buf), NUMECHO, "1");
    num = atoi(buf);
    if (num < 1)
	return;
    reload_money();
    if (cuser.money/money >= num) {
	*item += num;
        ch_pay(money * num, "寵物商店");
	show_chicken_picture(picture);
        pressanykey();
    } else {
	vmsg("現金不夠 !!!");
    }
    usleep(100000); // sleep 0.1s
}

static void
ch_eatoo(chicken_t *mychicken)
{
    if (mychicken->oo > 0) {
	mychicken->oo--;
	mychicken->tired = 0;
	if (mychicken->happy > 5)
	    mychicken->happy -= 5;
	show_chicken_picture(CHICKEN_PIC "/oo");
	pressanykey();
    }
}

static void
ch_eatmedicine(chicken_t *mychicken)
{
    if (mychicken->medicine > 0) {
	mychicken->medicine--;
	mychicken->sick = 0;
	if (mychicken->hp_max > 10)
	    mychicken->hp_max -= 3;
	mychicken->hp = mychicken->hp_max;
	if (mychicken->happy > 10)
	    mychicken->happy -= 10;
	show_chicken_picture(CHICKEN_PIC "/medicine");
	pressanykey();
    }
}

static void
ch_kill(chicken_t *mychicken)
{
    int        ans;

    ans = vans("棄養要被罰 100 元, 是否要棄養?(y/N)");
    if (ans == 'y') {

	ch_pay(100, "棄養寵物費");
	more(CHICKEN_PIC "/deadth", YEA);
	log_filef(CHICKENLOG, LOG_CREAT,
		 ANSI_COLOR(31) "%s " ANSI_RESET "把 "
                 ANSI_COLOR(33) "%s" ANSI_RESET ANSI_COLOR(32) " %s "
		 ANSI_RESET "宰了 於 %s\n", cuser.userid, mychicken->name,
		 chicken_type[(int)mychicken->type], Cdate(&now));
	mychicken->name[0] = 0;
    }
}

static void
ch_getting_old(int *hp, int *weight, int diff, int age)
{
    float           ex = 0.9;

    if (age > 70)
	ex = 0.1;
    else if (age > 30)
	ex = 0.5;
    else if (age > 20)
	ex = 0.7;

    diff /= 60 * 6;
    while (diff--) {
	*hp *= ex;
	*weight *= ex;
    }
}

static void debug_rpt(const char *msg GCC_UNUSED, chicken_t *c GCC_UNUSED) {
#ifdef DBG_CHICKEN
    if (!msg) {
        msg = "";
        clear();
    }
    if (c) {
        prints("%s hp:%d/%d weight:%d sick:%d dirty:%d\n",
               msg, c->hp, c->hp_max, c->weight, c->sick, c->dirty);
    } else {
        vmsg(msg);
    }
#endif
}

static void debug_timediff(chicken_t *c GCC_UNUSED) {
#ifdef DBG_CHICKEN
    char buf[7];
    vs_hdr2("寵物", "測試");
    getdata(2, 0, "要模擬幾小時未進入的狀態？", buf, sizeof(buf), NUMECHO);
    syncnow();
    if (*buf)
        c->lastvisit = now - atoi(buf) * 60 * 60;
#endif
}

/* 依時間變動的資料 */
static void
time_diff(chicken_t * thechicken)
{
    int             diff;
    int             theage = ((now - thechicken->cbirth) / DAY_SECONDS);
    const short *delta = time_change[(int)thechicken->type];

    thechicken->type %= NUM_KINDS;
    diff = (now - thechicken->lastvisit) / 60;

    if (diff < 1)
	return;

    debug_rpt(NULL, thechicken);

    if (theage > 13)		/* 老死 */
	ch_getting_old(&thechicken->hp_max, &thechicken->weight, diff, theage);
    debug_rpt("AFTER getting_old", thechicken);

    thechicken->lastvisit = now;
    // 注意: 從前有個腦殘把顯示的 weight 改成 (max_hp + weight / 50) / 100
    // 加上從前的老化公式導致一堆人的寵都是 weight=1 ，從此變出了所謂暗黑養法。
    // 一堆爛掉失衡的公式無從修起，就亂搞吧。
    thechicken->weight -= thechicken->hp_max * diff / 540;	/* 體重 */
    if (thechicken->weight < 1) {
	thechicken->sick -= thechicken->weight / 10;	/* 餓得病氣上升 */
	thechicken->weight = 1;
    }
    debug_rpt("AFTER weight-hp_max", thechicken);

    /* 快樂度 */
    thechicken->happy -= diff / 60;
    if (thechicken->happy < 0)
	thechicken->happy = 0;

    /* 學問 */
    thechicken->book -= delta[BOOK] * diff / (60 * 32);
    if (thechicken->book < 0)
	thechicken->book = 0;

    /* 滿意度 */
    thechicken->satis -= (diff / 180.0 * delta[SATIS]);
    if (thechicken->satis < 0)
	thechicken->satis = 0;

    /* 髒亂 */
    thechicken->dirty += diff * delta[DIRTY] / 30;
    debug_rpt("AFTER dirty+=diff*delta/30", thechicken);

    /* 生病(髒亂/體重) */
    if (thechicken->dirty > 1000)
	thechicken->sick += (thechicken->dirty - 400) / 10;
    if (thechicken->weight > 1)
	thechicken->sick -= diff / 60;
    debug_rpt("AFTER weight>1?sick-=diff/60", thechicken);

    /* 病氣恢護 */
    if (thechicken->sick < 0)
	thechicken->sick = 0;
    thechicken->tired -= (diff * delta[TIREDSTRONG] / 4);

    /* 疲勞 */
    if (thechicken->tired < 0)
	thechicken->tired = 0;

    /* hp_max */
    if (thechicken->hp >= thechicken->hp_max / 2)
	thechicken->hp_max += delta[HP_MAX] * diff / (60 * 12);
    /* hp恢護 */
    if (!thechicken->sick)
	thechicken->hp += delta[HP_MAX] * diff / (60 * 6);
    if (thechicken->hp > thechicken->hp_max)
	thechicken->hp = thechicken->hp_max;
    debug_rpt("AFTER hp-recovery", thechicken);
    debug_rpt("time_diff", NULL);
}

static void
check_sick(chicken_t *mychicken)
{
    /* 髒病的 */
    if (mychicken->tired > mychicken->hp * 0.3 && mychicken->dirty > 150)
	mychicken->sick += (mychicken->dirty - 150) / 10;
    /* 累病的 */
    if (mychicken->tired > mychicken->hp * 1.3)
	mychicken->sick += time_change[(int)mychicken->type][SICK];
    /* 病氣太重還做事減hp */
    if (mychicken->sick > mychicken->hp / 5) {
	mychicken->hp -= (mychicken->sick - mychicken->hp / 5) / 4;
	if (mychicken->hp < 0)
	    mychicken->hp = 0;
    }
}

static int
revive_chicken(chicken_t *thechicken, int admin)
{
    int c = 0;
    assert(thechicken);
    // check deadtype for what to do

    strlcpy(thechicken->name, "[撿回來的]", sizeof(thechicken->name));

    c = thechicken->hp_max / 5 +1;
    if (c < 2)	    c = 2;
    if (c > thechicken->hp_max) c = thechicken->hp_max;
    thechicken->hp = c;
    if (admin)
	thechicken->hp = thechicken->hp_max;

    thechicken->weight = thechicken->hp; // weight = 1 時 sick 有機再次死亡
    if (thechicken->weight < 2)	// 避免病死或餓死
	thechicken->weight = 2;

    thechicken->satis = 2; // 滿意降低
    thechicken->tired = thechicken->hp; // 若歸零則過太爽

    if (admin)
    {
	thechicken->sick = 0;
	thechicken->tired = 0;
    }

    thechicken->lastvisit = now; // really need so?
#ifdef HAVE_CHICKEN_CS
    revive_chicken_cs(thechicken);
#endif
    return 0;
}

enum DEADTYPE {
    DEADTYPE_NOTYET,
    DEADTYPE_NOHP,
    DEADTYPE_TOOTIRED,
    DEADTYPE_TOOFAT,
    DEADTYPE_NOFOOD,
    DEADTYPE_NOSATIS,
    DEADTYPE_UNKNOWN,
};

static int
deadtype(const chicken_t * thechicken, chicken_t *mychicken)
{
    int             i;

    if (thechicken->hp <= 0) // hp用盡
	i = DEADTYPE_NOHP;
    else if (thechicken->tired > thechicken->hp * 3) // 操勞過度
	i = DEADTYPE_TOOTIRED;
    else if (thechicken->weight > thechicken->hp_max * 5) // 肥胖過度
	i = DEADTYPE_TOOFAT;
    else if (thechicken->weight == 1 &&
	     thechicken->sick > thechicken->hp_max / 4) // 餓死了
	i = DEADTYPE_NOFOOD;
    else if (thechicken->satis <= 0) // 很不滿意
	i = DEADTYPE_NOSATIS;
    else
	return DEADTYPE_NOTYET;

    if (thechicken == mychicken) {
	log_filef(CHICKENLOG, LOG_CREAT,
                 ANSI_COLOR(31) "%s" ANSI_RESET " 所疼愛的"
                 ANSI_COLOR(33) " %s" ANSI_COLOR(32) " %s "
                 ANSI_RESET "掛了 於 %s\n", cuser.userid, thechicken->name,
                 chicken_type[(int)thechicken->type], Cdate(&now));
	mychicken->name[0] = 0;
    }
    return i;
}

static int
showdeath(int type)
{
    char fn[MAXPATHLEN];
    const char *death_file_names[] = {
        "nohp",
        "tootired",
        "toofat",
        "nofood",
        "nosatis",
    };
    if (type <= DEADTYPE_NOTYET || type >= DEADTYPE_UNKNOWN)
        return 0;

    snprintf(fn, sizeof(fn), CHICKEN_PIC "/%s", death_file_names[type - 1]);
    more(fn, YEA);
    // "deadth" here is a typo, but let's take it (otherwise we need to also
    // change resource file name).
    more(CHICKEN_PIC "/deadth", YEA);
    return type;
}

static int
isdeath(const chicken_t * thechicken, chicken_t *mychicken)
{
    int             i;

    if (!(i = deadtype(thechicken, mychicken)))
	return 0;
    return showdeath(i);
}

static void
ch_changename(chicken_t *mychicken)
{
    char      newname[20] = "";

    getdata_str(b_lines - 1, 0, "改個好名字吧: ", newname, sizeof(newname)-1,
                DOECHO, mychicken->name);

    if (strlen(newname) >= 3 && strcmp(newname, mychicken->name)) {
	strlcpy(mychicken->name, newname, sizeof(mychicken->name));
	log_filef(CHICKENLOG, LOG_CREAT,
                ANSI_COLOR(31) "%s" ANSI_RESET " 把疼愛的" ANSI_COLOR(33)
                " %s" ANSI_COLOR(32) " %s "
                ANSI_RESET "改名為" ANSI_COLOR(33) " %s" ANSI_RESET " 於 %s\n",
                 cuser.userid, mychicken->name,
                 chicken_type[(int)mychicken->type], newname, Cdate(&now));
    }
}

static int
select_menu(int age GCC_UNUSED, chicken_t *mychicken)
{
    char            ch;

    reload_money();
    move(19, 0);

    vbarf(ANSI_COLOR(44;37) " " MONEYNAME ":" ANSI_COLOR(33) " %-10d"
#ifdef HAVE_CHICKEN_CS
          ANSI_COLOR(37) "  常識點數 :" ANSI_COLOR(33) " %-10d"
#endif
          , cuser.money
#ifdef HAVE_CHICKEN_CS
          , mychicken->commonsense
#endif
          );

    prints("\n" ANSI_COLOR(33) "(" ANSI_COLOR(37) "1" ANSI_COLOR(33) ")清理 "
           "(" ANSI_COLOR(37) "2" ANSI_COLOR(33) ")吃飯 "
	   "(" ANSI_COLOR(37) "3" ANSI_COLOR(33) ")猜拳   "
           "(" ANSI_COLOR(37) "4" ANSI_COLOR(33) ")唸書 "
	   "(" ANSI_COLOR(37) "5" ANSI_COLOR(33) ")親他 "
           "(" ANSI_COLOR(37) "6" ANSI_COLOR(33) ")打他 "
	   "(" ANSI_COLOR(37) "7" ANSI_COLOR(33) ")買%s "
           "(" ANSI_COLOR(37) "8" ANSI_COLOR(33) ")吃補品\n"
	   "(" ANSI_COLOR(37) "9" ANSI_COLOR(33) ")吃藥 "
	   "(" ANSI_COLOR(37) "m" ANSI_COLOR(33) ")買藥 "
           "(" ANSI_COLOR(37) "o" ANSI_COLOR(33) ")買補品 "
           "(" ANSI_COLOR(37) "k" ANSI_COLOR(33) ")棄養 "
	   "(" ANSI_COLOR(37) "n" ANSI_COLOR(33) ")改名 "
#ifdef HAVE_CHICKEN_CS
	   "(" ANSI_COLOR(37) "s" ANSI_COLOR(33) ")常識問答 "
#endif
	   "(" ANSI_COLOR(37) "q" ANSI_COLOR(33) ")離開:" ANSI_RESET,
	   chicken_food[(int)mychicken->type]);
    do {
	switch (ch = vkey()) {
	case '1':
	    ch_clean(mychicken);
	    check_sick(mychicken);
	    break;
	case '2':
	    ch_eat(mychicken);
	    check_sick(mychicken);
	    break;
	case '3':
	    ch_guess(mychicken);
	    check_sick(mychicken);
	    break;
	case '4':
	    ch_book(mychicken);
	    check_sick(mychicken);
	    break;
	case '5':
	    ch_kiss(mychicken);
	    break;
	case '6':
	    ch_hit(mychicken);
	    check_sick(mychicken);
	    break;
	case '7':
	    ch_buyitem(food_price[(int)mychicken->type], CHICKEN_PIC "/food",
		       &mychicken->food,  mychicken);
	    break;
	case '8':
	    ch_eatoo(mychicken);
	    break;
	case '9':
	    ch_eatmedicine(mychicken);
	    break;
	case 'O':
	case 'o':
	    ch_buyitem(100, CHICKEN_PIC "/buyoo", &mychicken->oo, mychicken);
	    break;
	case 'M':
	case 'm':
            ch_buyitem(10, CHICKEN_PIC "/buymedicine", &mychicken->medicine,
                       mychicken);
	    break;
	case 'N':
	case 'n':
	    ch_changename(mychicken);
	    break;
	case 'K':
	case 'k':
	    ch_kill(mychicken);
	    return 0;
	case 'Q':
	case 'q':
	    return 0;
#ifdef HAVE_CHICKEN_CS
	case 'S':
	case 's':
	    ch_teach(mychicken);
	    break;
#endif
	}
    } while (ch < ' ' || ch > 'z');
    return 1;
}

static int
recover_chicken(chicken_t * thechicken)
{
    char            buf[200];
    int             price = egg_price[(int)thechicken->type];
    int		    money = price;

    // money is a little less than price.
#ifdef HAVE_CHICKEN_CS
    if (recover_chicken_cs(thechicken))
        return 1;
#endif

    money = price + (random() % price);
    price *= 2;

    if (now - thechicken->lastvisit > (60 * 60 * 24 * 7))
	return 0;

    vs_hdr2(" 養雞場 ", " 復活寵物");
    prints("\n你有一個剛死亡不久的 %s 要招喚回來嗎? 只要 %d 元唷...\n",
           chicken_type[(int)thechicken->type], price);
    do {
	getdata(10, 0, " 要花錢復活寵物嗎？ [y/n]: ",
		buf, 2, LCECHO);
    }
    while (buf[0] != 'y' && buf[0] != 'n');

    if (buf[0] == 'n') {
        thechicken->lastvisit = 0;
        return 0;
    }

    reload_money();
    if (cuser.money < price) {
        vmsg("錢不夠喔，請籌錢後再來");
        return 0;
    }

    ch_pay(money, "靈界守衛");
    revive_chicken(thechicken, 0);
    move(12, 0);
    prints("寵物已復活。 請記得加以餵食避免再度死亡。\n"
           "感謝您對寵物的愛心，復活費打折，僅收 $%d。\n", money);
    pressanykey();
    return 1;
}

void
chicken_toggle_death(const char *uid)
{
    chicken_t *mychicken = load_live_chicken(uid);

    assert(uid);
    if (!mychicken)
    {
	vmsgf("%s 沒養寵物。", uid);
    }
    else if (mychicken->name[0])
    {
	mychicken->name[0] = 0;
	vmsgf("%s 的寵物被殺死了", uid);
    }
    else
    {
	if (restore_last_seen(uid, mychicken)) {
	    vmsgf("%s 的寵物已回到上次看到牠的狀態", uid);
	    free_live_chicken(mychicken);
	    return;
	}
	revive_chicken(mychicken, 1);
	strlcpy(mychicken->name, "[死]", sizeof(mychicken->name));
	vmsgf("%s 的寵物復活了", uid);
    }
    free_live_chicken(mychicken);
}

void
chicken_query(const char *userid)
{
    chicken_t xchicken;

    if (!load_chicken(userid, &xchicken))
    {
	move(1, 0);
	clrtobot();
	prints("\n\n%s 並沒有養寵物..", userid);
    } else {
        debug_timediff(&xchicken);
	time_diff(&xchicken);
	if (!isdeath(&xchicken, NULL))
	{
	    show_chicken_data(&xchicken);
#ifdef HAVE_CHICKEN_CS
            prints("常識點數: %d", xchicken.commonsense);
#endif
	    prints("\n\n以上是 %s 的寵物資料..", userid);
	} else {
	    move(1, 0);
	    clrtobot();
	    prints("\n\n%s 的寵物死掉了...", userid);
	}
    }

    pressanykey();
}


/* MilkTea: SYSOP / retired SYSOP pets may be rescued from death.
 * Goes back to the state the owner last saw alive; time since then is forgiven.
 * Returns 1 if rescued (mychicken restored), 0 if the pet is left to die. */
static int
chicken_god_rescue(int cause, const chicken_t *safe, chicken_t *mychicken)
{
    char buf[128];
    const char *why;
    int ans;

    switch (cause) {
    case DEADTYPE_NOHP:
	why = "體力用盡";
	break;
    case DEADTYPE_TOOTIRED:
	why = "操勞過度";
	break;
    case DEADTYPE_TOOFAT:
	why = "肥胖過度";
	break;
    case DEADTYPE_NOFOOD:
	why = "餓壞了";
	break;
    case DEADTYPE_NOSATIS:
	why = "太不滿意";
	break;
    default:
	why = "不明原因";
	break;
    }

    snprintf(buf, sizeof(buf), "發現 %s 導致死亡，你要動用大神的能力救回他嗎 [Y/n]", why);
    ans = vans(buf);
    if (ans == 'n' || ans == 'N')
	return 0;

    log_filef(CHICKENLOG, LOG_CREAT,
	      ANSI_COLOR(31) "%s" ANSI_RESET " 動用大神的能力救回了 "
	      ANSI_COLOR(33) "%s" ANSI_COLOR(32) " %s " ANSI_RESET
	      "(%s) 於 %s\n", cuser.userid, safe->name,
	      chicken_type[(int)safe->type], why, Cdate(&now));

    memcpy(mychicken, safe, sizeof(chicken_t));
    mychicken->lastvisit = now;
    vmsg("大神出手，牠已回到你上次看到的樣子");
    return 1;
}

#define lockreturn0(unmode, state) if(lockutmpmode(unmode, state)) return 0

int
chicken_main(void)
{
    int age;
    chicken_t *mychicken;
    chicken_t safe;
    int has_safe = 0, sysop_rescue, cause;
    lockreturn0(CHICKEN, LOCK_MULTI);
    ticket_issue();
    mychicken = load_live_chicken(cuser.userid);
    debug_timediff(mychicken);
    if (mychicken && !mychicken->name[0])
    {
	// possible for recovery
	recover_chicken(mychicken);
    }
    if (!mychicken || !mychicken->name[0])
    {
	free_live_chicken(mychicken);
	mychicken = NULL;

	// create new?
	if (new_chicken())
	    mychicken = load_live_chicken(cuser.userid);

	// exit if still no valid data.
	if (!mychicken || !mychicken->name[0])
	{
	    ticket_finish(TK_VOID);
	    unlockutmpmode();
	    free_live_chicken(mychicken);
	    return 0;
	}
    }
    assert(mychicken);
    age = ((now - mychicken->cbirth) / DAY_SECONDS);
    // MilkTea: for SYSOP / retired SYSOP keep "safe" = the last state seen
    // alive (before the first time_diff, and again after each screen is
    // shown, i.e. before each action). A death is undone by going back to it.
    sysop_rescue = HasUserPerm(PERM_SYSOP | PERM_OLDSYSOP);
    if (sysop_rescue && deadtype(mychicken, NULL) == DEADTYPE_NOTYET) {
	memcpy(&safe, mychicken, sizeof(chicken_t));
	has_safe = 1;
    }
    do {
	time_diff(mychicken);
	if (sysop_rescue && has_safe &&
	    (cause = deadtype(mychicken, NULL)) != DEADTYPE_NOTYET &&
	    chicken_god_rescue(cause, &safe, mychicken)) {
	    // rescued: mychicken is back to the safe state, show it as usual
	} else if (isdeath(mychicken, mychicken))
	    break;
	show_chicken_data(mychicken);
	if (sysop_rescue && deadtype(mychicken, NULL) == DEADTYPE_NOTYET) {
	    memcpy(&safe, mychicken, sizeof(chicken_t));
	    has_safe = 1;
	}
    } while (select_menu(age, mychicken));
    // MilkTea: leaving while alive -> remember how the owner last saw it
    if (mychicken->name[0])
	save_last_seen(cuser.userid, mychicken);
    // MilkTea: MtBBS-App tickets. Wait at a blank input line for the App's report.
    if (mychicken->name[0])
	ticket_leave(mychicken);
    else
	ticket_finish(TK_VOID);
    unlockutmpmode();
    free_live_chicken(mychicken);
    return 0;
}
