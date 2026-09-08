/**
 * cities.cpp —— 地级行政区名录(直辖市 / 地级市 / 地区 / 盟 / 自治州 / 港澳台)
 */
#include "cities.h"

#include <ctype.h>
#include <string.h>

namespace Cities {

const char* const PROV_NAMES[] = {
    "直辖市",   "河北",     "山西",     "内蒙古", "辽宁",   "吉林",
    "黑龙江",   "江苏",     "浙江",     "安徽",   "福建",   "江西",
    "山东",     "河南",     "湖北",     "湖南",   "广东",   "广西",
    "海南",     "四川",     "贵州",     "云南",   "西藏",   "陕西",
    "甘肃",     "青海",     "宁夏",     "新疆",   "港澳台",
};
const int PROV_N = sizeof(PROV_NAMES) / sizeof(PROV_NAMES[0]);

const Rec LIST[] = {
    // 直辖市
    {"北京", "bei jing", 0, 39.90f, 116.41f},
    {"天津", "tian jin", 0, 39.12f, 117.20f},
    {"上海", "shang hai", 0, 31.23f, 121.47f},
    {"重庆", "chong qing", 0, 29.56f, 106.55f},
    // 河北
    {"石家庄", "shi jia zhuang", 1, 38.04f, 114.51f},
    {"唐山", "tang shan", 1, 39.63f, 118.18f},
    {"秦皇岛", "qin huang dao", 1, 39.94f, 119.60f},
    {"邯郸", "han dan", 1, 36.63f, 114.49f},
    {"邢台", "xing tai", 1, 37.07f, 114.50f},
    {"保定", "bao ding", 1, 38.87f, 115.46f},
    {"张家口", "zhang jia kou", 1, 40.82f, 114.89f},
    {"承德", "cheng de", 1, 40.95f, 117.96f},
    {"沧州", "cang zhou", 1, 38.30f, 116.84f},
    {"廊坊", "lang fang", 1, 39.52f, 116.70f},
    {"衡水", "heng shui", 1, 37.74f, 115.67f},
    // 山西
    {"太原", "tai yuan", 2, 37.87f, 112.55f},
    {"大同", "da tong", 2, 40.08f, 113.30f},
    {"阳泉", "yang quan", 2, 37.86f, 113.58f},
    {"长治", "chang zhi", 2, 36.20f, 113.12f},
    {"晋城", "jin cheng", 2, 35.49f, 112.85f},
    {"朔州", "shuo zhou", 2, 39.33f, 112.43f},
    {"晋中", "jin zhong", 2, 37.69f, 112.75f},
    {"运城", "yun cheng", 2, 35.03f, 111.01f},
    {"忻州", "xin zhou", 2, 38.42f, 112.73f},
    {"临汾", "lin fen", 2, 36.09f, 111.52f},
    {"吕梁", "lv liang", 2, 37.52f, 111.14f},
    // 内蒙古
    {"呼和浩特", "hu he hao te", 3, 40.84f, 111.75f},
    {"包头", "bao tou", 3, 40.66f, 109.84f},
    {"乌海", "wu hai", 3, 39.67f, 106.83f},
    {"赤峰", "chi feng", 3, 42.27f, 118.89f},
    {"通辽", "tong liao", 3, 43.65f, 122.26f},
    {"鄂尔多斯", "e er duo si", 3, 39.61f, 109.78f},
    {"呼伦贝尔", "hu lun bei er", 3, 49.22f, 119.77f},
    {"巴彦淖尔", "ba yan nao er", 3, 40.74f, 107.39f},
    {"乌兰察布", "wu lan cha bu", 3, 41.03f, 113.13f},
    {"兴安盟", "xing an meng", 3, 46.08f, 122.04f},
    {"锡林郭勒盟", "xi lin guo le meng", 3, 43.93f, 116.05f},
    {"阿拉善盟", "a la shan meng", 3, 38.85f, 105.73f},
    // 辽宁
    {"沈阳", "shen yang", 4, 41.81f, 123.43f},
    {"大连", "da lian", 4, 38.91f, 121.61f},
    {"鞍山", "an shan", 4, 41.11f, 122.99f},
    {"抚顺", "fu shun", 4, 41.88f, 123.96f},
    {"本溪", "ben xi", 4, 41.30f, 123.77f},
    {"丹东", "dan dong", 4, 40.13f, 124.38f},
    {"锦州", "jin zhou", 4, 41.10f, 121.14f},
    {"营口", "ying kou", 4, 40.67f, 122.24f},
    {"阜新", "fu xin", 4, 42.02f, 121.67f},
    {"辽阳", "liao yang", 4, 41.27f, 123.24f},
    {"盘锦", "pan jin", 4, 41.12f, 122.07f},
    {"铁岭", "tie ling", 4, 42.29f, 123.84f},
    {"朝阳", "chao yang", 4, 41.57f, 120.45f},
    {"葫芦岛", "hu lu dao", 4, 40.71f, 120.84f},
    // 吉林
    {"长春", "chang chun", 5, 43.82f, 125.32f},
    {"吉林", "ji lin", 5, 43.84f, 126.55f},
    {"四平", "si ping", 5, 43.17f, 124.35f},
    {"辽源", "liao yuan", 5, 42.89f, 125.14f},
    {"通化", "tong hua", 5, 41.73f, 125.94f},
    {"白山", "bai shan", 5, 41.94f, 126.42f},
    {"松原", "song yuan", 5, 45.14f, 124.83f},
    {"白城", "bai cheng", 5, 45.62f, 122.84f},
    {"延边", "yan bian", 5, 42.89f, 129.51f},
    // 黑龙江
    {"哈尔滨", "ha er bin", 6, 45.80f, 126.54f},
    {"齐齐哈尔", "qi qi ha er", 6, 47.35f, 123.92f},
    {"鸡西", "ji xi", 6, 45.30f, 130.97f},
    {"鹤岗", "he gang", 6, 47.33f, 130.30f},
    {"双鸭山", "shuang ya shan", 6, 46.65f, 131.16f},
    {"大庆", "da qing", 6, 46.59f, 125.10f},
    {"伊春", "yi chun", 6, 47.73f, 128.90f},
    {"佳木斯", "jia mu si", 6, 46.80f, 130.32f},
    {"七台河", "qi tai he", 6, 45.77f, 131.00f},
    {"牡丹江", "mu dan jiang", 6, 44.55f, 129.63f},
    {"黑河", "hei he", 6, 50.25f, 127.53f},
    {"绥化", "sui hua", 6, 46.64f, 126.99f},
    {"大兴安岭", "da xing an ling", 6, 52.34f, 124.71f},
    // 江苏
    {"南京", "nan jing", 7, 32.06f, 118.80f},
    {"无锡", "wu xi", 7, 31.49f, 120.31f},
    {"徐州", "xu zhou", 7, 34.20f, 117.28f},
    {"常州", "chang zhou", 7, 31.81f, 119.97f},
    {"苏州", "su zhou", 7, 31.30f, 120.58f},
    {"南通", "nan tong", 7, 32.02f, 120.89f},
    {"连云港", "lian yun gang", 7, 34.60f, 119.22f},
    {"淮安", "huai an", 7, 33.55f, 119.11f},
    {"盐城", "yan cheng", 7, 33.35f, 120.16f},
    {"扬州", "yang zhou", 7, 32.39f, 119.42f},
    {"镇江", "zhen jiang", 7, 32.19f, 119.45f},
    {"泰州", "tai zhou", 7, 32.48f, 119.92f},
    {"宿迁", "su qian", 7, 33.96f, 118.28f},
    // 浙江
    {"杭州", "hang zhou", 8, 30.27f, 120.15f},
    {"宁波", "ning bo", 8, 29.87f, 121.54f},
    {"温州", "wen zhou", 8, 28.00f, 120.70f},
    {"嘉兴", "jia xing", 8, 30.75f, 120.76f},
    {"湖州", "hu zhou", 8, 30.89f, 120.09f},
    {"绍兴", "shao xing", 8, 30.00f, 120.58f},
    {"金华", "jin hua", 8, 29.08f, 119.65f},
    {"衢州", "qu zhou", 8, 28.97f, 118.87f},
    {"舟山", "zhou shan", 8, 30.00f, 122.21f},
    {"台州", "tai zhou", 8, 28.66f, 121.42f},
    {"丽水", "li shui", 8, 28.45f, 119.92f},
    // 安徽
    {"合肥", "he fei", 9, 31.82f, 117.23f},
    {"芜湖", "wu hu", 9, 31.35f, 118.43f},
    {"蚌埠", "beng bu", 9, 32.92f, 117.39f},
    {"淮南", "huai nan", 9, 32.63f, 117.00f},
    {"马鞍山", "ma an shan", 9, 31.67f, 118.51f},
    {"淮北", "huai bei", 9, 33.95f, 116.80f},
    {"铜陵", "tong ling", 9, 30.94f, 117.81f},
    {"安庆", "an qing", 9, 30.53f, 117.05f},
    {"黄山", "huang shan", 9, 29.71f, 118.34f},
    {"滁州", "chu zhou", 9, 32.26f, 118.32f},
    {"阜阳", "fu yang", 9, 32.90f, 115.81f},
    {"宿州", "su zhou", 9, 33.65f, 116.96f},
    {"六安", "lu an", 9, 31.73f, 116.51f},
    {"亳州", "bo zhou", 9, 33.84f, 115.78f},
    {"池州", "chi zhou", 9, 30.67f, 117.49f},
    {"宣城", "xuan cheng", 9, 30.95f, 118.76f},
    // 福建
    {"福州", "fu zhou", 10, 26.07f, 119.30f},
    {"厦门", "xia men", 10, 24.48f, 118.09f},
    {"莆田", "pu tian", 10, 25.45f, 119.01f},
    {"三明", "san ming", 10, 26.27f, 117.64f},
    {"泉州", "quan zhou", 10, 24.87f, 118.68f},
    {"漳州", "zhang zhou", 10, 24.51f, 117.65f},
    {"南平", "nan ping", 10, 26.64f, 118.18f},
    {"龙岩", "long yan", 10, 25.08f, 117.03f},
    {"宁德", "ning de", 10, 26.67f, 119.55f},
    // 江西
    {"南昌", "nan chang", 11, 28.68f, 115.86f},
    {"景德镇", "jing de zhen", 11, 29.27f, 117.18f},
    {"萍乡", "ping xiang", 11, 27.62f, 113.85f},
    {"九江", "jiu jiang", 11, 29.71f, 116.00f},
    {"新余", "xin yu", 11, 27.82f, 114.92f},
    {"鹰潭", "ying tan", 11, 28.26f, 117.07f},
    {"赣州", "gan zhou", 11, 25.83f, 114.93f},
    {"吉安", "ji an", 11, 27.11f, 114.97f},
    {"宜春", "yi chun", 11, 27.80f, 114.42f},
    {"抚州", "fu zhou", 11, 27.95f, 116.36f},
    {"上饶", "shang rao", 11, 28.45f, 117.94f},
    // 山东
    {"济南", "ji nan", 12, 36.65f, 117.12f},
    {"青岛", "qing dao", 12, 36.07f, 120.38f},
    {"淄博", "zi bo", 12, 36.81f, 118.05f},
    {"枣庄", "zao zhuang", 12, 34.81f, 117.32f},
    {"东营", "dong ying", 12, 37.43f, 118.67f},
    {"烟台", "yan tai", 12, 37.46f, 121.45f},
    {"潍坊", "wei fang", 12, 36.71f, 119.16f},
    {"济宁", "ji ning", 12, 35.41f, 116.59f},
    {"泰安", "tai an", 12, 36.20f, 117.09f},
    {"威海", "wei hai", 12, 37.51f, 122.12f},
    {"日照", "ri zhao", 12, 35.42f, 119.53f},
    {"临沂", "lin yi", 12, 35.10f, 118.36f},
    {"德州", "de zhou", 12, 37.44f, 116.36f},
    {"聊城", "liao cheng", 12, 36.46f, 115.99f},
    {"滨州", "bin zhou", 12, 37.38f, 117.97f},
    {"菏泽", "he ze", 12, 35.23f, 115.48f},
    // 河南
    {"郑州", "zheng zhou", 13, 34.75f, 113.65f},
    {"开封", "kai feng", 13, 34.80f, 114.31f},
    {"洛阳", "luo yang", 13, 34.62f, 112.45f},
    {"平顶山", "ping ding shan", 13, 33.77f, 113.19f},
    {"安阳", "an yang", 13, 36.10f, 114.35f},
    {"鹤壁", "he bi", 13, 35.75f, 114.30f},
    {"新乡", "xin xiang", 13, 35.30f, 113.93f},
    {"焦作", "jiao zuo", 13, 35.22f, 113.24f},
    {"濮阳", "pu yang", 13, 35.77f, 115.03f},
    {"许昌", "xu chang", 13, 34.04f, 113.83f},
    {"漯河", "luo he", 13, 33.58f, 114.02f},
    {"三门峡", "san men xia", 13, 34.77f, 111.20f},
    {"南阳", "nan yang", 13, 32.99f, 112.53f},
    {"商丘", "shang qiu", 13, 34.41f, 115.65f},
    {"信阳", "xin yang", 13, 32.15f, 114.09f},
    {"周口", "zhou kou", 13, 33.63f, 114.70f},
    {"驻马店", "zhu ma dian", 13, 33.01f, 114.02f},
    {"济源", "ji yuan", 13, 35.07f, 112.60f},
    // 湖北
    {"武汉", "wu han", 14, 30.59f, 114.31f},
    {"黄石", "huang shi", 14, 30.20f, 115.04f},
    {"十堰", "shi yan", 14, 32.63f, 110.80f},
    {"宜昌", "yi chang", 14, 30.69f, 111.29f},
    {"襄阳", "xiang yang", 14, 32.01f, 112.12f},
    {"鄂州", "e zhou", 14, 30.39f, 114.89f},
    {"荆门", "jing men", 14, 31.04f, 112.20f},
    {"孝感", "xiao gan", 14, 30.92f, 113.95f},
    {"荆州", "jing zhou", 14, 30.33f, 112.24f},
    {"黄冈", "huang gang", 14, 30.45f, 114.87f},
    {"咸宁", "xian ning", 14, 29.84f, 114.32f},
    {"随州", "sui zhou", 14, 31.69f, 113.38f},
    {"恩施", "en shi", 14, 30.27f, 109.49f},
    {"仙桃", "xian tao", 14, 30.36f, 113.45f},
    {"潜江", "qian jiang", 14, 30.40f, 112.90f},
    {"天门", "tian men", 14, 30.66f, 113.17f},
    {"神农架", "shen nong jia", 14, 31.74f, 110.68f},
    // 湖南
    {"长沙", "chang sha", 15, 28.23f, 112.94f},
    {"株洲", "zhu zhou", 15, 27.83f, 113.13f},
    {"湘潭", "xiang tan", 15, 27.83f, 112.94f},
    {"衡阳", "heng yang", 15, 26.89f, 112.57f},
    {"邵阳", "shao yang", 15, 27.24f, 111.47f},
    {"岳阳", "yue yang", 15, 29.36f, 113.13f},
    {"常德", "chang de", 15, 29.03f, 111.70f},
    {"张家界", "zhang jia jie", 15, 29.12f, 110.48f},
    {"益阳", "yi yang", 15, 28.55f, 112.36f},
    {"郴州", "chen zhou", 15, 25.77f, 113.02f},
    {"永州", "yong zhou", 15, 26.42f, 111.61f},
    {"怀化", "huai hua", 15, 27.55f, 110.00f},
    {"娄底", "lou di", 15, 27.70f, 111.99f},
    {"湘西", "xiang xi", 15, 28.31f, 109.74f},
    // 广东
    {"广州", "guang zhou", 16, 23.13f, 113.26f},
    {"韶关", "shao guan", 16, 24.80f, 113.60f},
    {"深圳", "shen zhen", 16, 22.54f, 114.06f},
    {"珠海", "zhu hai", 16, 22.27f, 113.58f},
    {"汕头", "shan tou", 16, 23.35f, 116.68f},
    {"佛山", "fo shan", 16, 23.02f, 113.12f},
    {"江门", "jiang men", 16, 22.58f, 113.08f},
    {"湛江", "zhan jiang", 16, 21.27f, 110.36f},
    {"茂名", "mao ming", 16, 21.66f, 110.93f},
    {"肇庆", "zhao qing", 16, 23.05f, 112.47f},
    {"惠州", "hui zhou", 16, 23.11f, 114.42f},
    {"梅州", "mei zhou", 16, 24.29f, 116.12f},
    {"汕尾", "shan wei", 16, 22.79f, 115.36f},
    {"河源", "he yuan", 16, 23.74f, 114.70f},
    {"阳江", "yang jiang", 16, 21.86f, 111.98f},
    {"清远", "qing yuan", 16, 23.68f, 113.06f},
    {"东莞", "dong guan", 16, 23.02f, 113.75f},
    {"中山", "zhong shan", 16, 22.52f, 113.39f},
    {"潮州", "chao zhou", 16, 23.66f, 116.62f},
    {"揭阳", "jie yang", 16, 23.55f, 116.37f},
    {"云浮", "yun fu", 16, 22.94f, 112.04f},
    // 广西
    {"南宁", "nan ning", 17, 22.82f, 108.37f},
    {"柳州", "liu zhou", 17, 24.31f, 109.41f},
    {"桂林", "gui lin", 17, 25.27f, 110.29f},
    {"梧州", "wu zhou", 17, 23.48f, 111.28f},
    {"北海", "bei hai", 17, 21.48f, 109.12f},
    {"防城港", "fang cheng gang", 17, 21.69f, 108.35f},
    {"钦州", "qin zhou", 17, 21.98f, 108.65f},
    {"贵港", "gui gang", 17, 23.11f, 109.60f},
    {"玉林", "yu lin", 17, 22.64f, 110.16f},
    {"百色", "bai se", 17, 23.90f, 106.62f},
    {"贺州", "he zhou", 17, 24.40f, 111.57f},
    {"河池", "he chi", 17, 24.69f, 108.09f},
    {"来宾", "lai bin", 17, 23.75f, 109.23f},
    {"崇左", "chong zuo", 17, 22.40f, 107.36f},
    // 海南
    {"海口", "hai kou", 18, 20.04f, 110.20f},
    {"三亚", "san ya", 18, 18.25f, 109.51f},
    {"三沙", "san sha", 18, 16.83f, 112.33f},
    {"儋州", "dan zhou", 18, 19.52f, 109.58f},
    // 四川
    {"成都", "cheng du", 19, 30.67f, 104.07f},
    {"自贡", "zi gong", 19, 29.34f, 104.78f},
    {"攀枝花", "pan zhi hua", 19, 26.58f, 101.72f},
    {"泸州", "lu zhou", 19, 28.87f, 105.44f},
    {"德阳", "de yang", 19, 31.13f, 104.40f},
    {"绵阳", "mian yang", 19, 31.47f, 104.73f},
    {"广元", "guang yuan", 19, 32.44f, 105.84f},
    {"遂宁", "sui ning", 19, 30.51f, 105.57f},
    {"内江", "nei jiang", 19, 29.58f, 105.07f},
    {"乐山", "le shan", 19, 29.55f, 103.77f},
    {"南充", "nan chong", 19, 30.80f, 106.08f},
    {"眉山", "mei shan", 19, 30.08f, 103.85f},
    {"宜宾", "yi bin", 19, 28.76f, 104.64f},
    {"广安", "guang an", 19, 30.46f, 106.63f},
    {"达州", "da zhou", 19, 31.21f, 107.50f},
    {"雅安", "ya an", 19, 30.01f, 103.04f},
    {"巴中", "ba zhong", 19, 31.87f, 106.75f},
    {"资阳", "zi yang", 19, 30.13f, 104.64f},
    {"阿坝", "a ba", 19, 31.90f, 102.22f},
    {"甘孜", "gan zi", 19, 30.05f, 101.96f},
    {"凉山", "liang shan", 19, 27.88f, 102.27f},
    // 贵州
    {"贵阳", "gui yang", 20, 26.65f, 106.63f},
    {"六盘水", "liu pan shui", 20, 26.59f, 104.83f},
    {"遵义", "zun yi", 20, 27.73f, 106.93f},
    {"安顺", "an shun", 20, 26.25f, 105.95f},
    {"毕节", "bi jie", 20, 27.30f, 105.29f},
    {"铜仁", "tong ren", 20, 27.73f, 109.19f},
    {"黔西南", "qian xi nan", 20, 25.09f, 104.90f},
    {"黔东南", "qian dong nan", 20, 26.58f, 107.98f},
    {"黔南", "qian nan", 20, 26.25f, 107.52f},
    // 云南
    {"昆明", "kun ming", 21, 25.04f, 102.71f},
    {"曲靖", "qu jing", 21, 25.49f, 103.80f},
    {"玉溪", "yu xi", 21, 24.35f, 102.54f},
    {"保山", "bao shan", 21, 25.11f, 99.16f},
    {"昭通", "zhao tong", 21, 27.34f, 103.72f},
    {"丽江", "li jiang", 21, 26.86f, 100.23f},
    {"普洱", "pu er", 21, 22.83f, 100.97f},
    {"临沧", "lin cang", 21, 23.88f, 100.09f},
    {"楚雄", "chu xiong", 21, 25.05f, 101.55f},
    {"红河", "hong he", 21, 23.36f, 103.38f},
    {"文山", "wen shan", 21, 23.37f, 104.24f},
    {"西双版纳", "xi shuang ban na", 21, 22.01f, 100.80f},
    {"大理", "da li", 21, 25.61f, 100.27f},
    {"德宏", "de hong", 21, 24.43f, 98.59f},
    {"怒江", "nu jiang", 21, 25.82f, 98.86f},
    {"迪庆", "di qing", 21, 27.82f, 99.71f},
    // 西藏
    {"拉萨", "la sa", 22, 29.65f, 91.12f},
    {"日喀则", "ri ka ze", 22, 29.27f, 88.88f},
    {"昌都", "chang du", 22, 31.14f, 97.18f},
    {"林芝", "lin zhi", 22, 29.65f, 94.36f},
    {"山南", "shan nan", 22, 29.24f, 91.77f},
    {"那曲", "na qu", 22, 31.48f, 92.05f},
    {"阿里", "a li", 22, 32.50f, 80.11f},
    // 陕西
    {"西安", "xi an", 23, 34.34f, 108.94f},
    {"铜川", "tong chuan", 23, 34.90f, 108.95f},
    {"宝鸡", "bao ji", 23, 34.36f, 107.24f},
    {"咸阳", "xian yang", 23, 34.33f, 108.71f},
    {"渭南", "wei nan", 23, 34.50f, 109.51f},
    {"延安", "yan an", 23, 36.59f, 109.49f},
    {"汉中", "han zhong", 23, 33.07f, 107.02f},
    {"榆林", "yu lin", 23, 38.29f, 109.73f},
    {"安康", "an kang", 23, 32.68f, 109.03f},
    {"商洛", "shang luo", 23, 33.87f, 109.94f},
    // 甘肃
    {"兰州", "lan zhou", 24, 36.06f, 103.83f},
    {"嘉峪关", "jia yu guan", 24, 39.77f, 98.29f},
    {"金昌", "jin chang", 24, 38.52f, 102.19f},
    {"白银", "bai yin", 24, 36.54f, 104.17f},
    {"天水", "tian shui", 24, 34.58f, 105.72f},
    {"武威", "wu wei", 24, 37.93f, 102.63f},
    {"张掖", "zhang ye", 24, 38.93f, 100.45f},
    {"平凉", "ping liang", 24, 35.54f, 106.68f},
    {"酒泉", "jiu quan", 24, 39.74f, 98.49f},
    {"庆阳", "qing yang", 24, 35.71f, 107.64f},
    {"定西", "ding xi", 24, 35.58f, 104.63f},
    {"陇南", "long nan", 24, 33.40f, 104.92f},
    {"临夏", "lin xia", 24, 35.60f, 103.21f},
    {"甘南", "gan nan", 24, 34.98f, 102.91f},
    // 青海
    {"西宁", "xi ning", 25, 36.62f, 101.78f},
    {"海东", "hai dong", 25, 36.50f, 102.10f},
    {"海北", "hai bei", 25, 36.95f, 100.90f},
    {"黄南", "huang nan", 25, 35.52f, 102.02f},
    {"海南州", "hai nan zhou", 25, 36.28f, 100.62f},
    {"果洛", "guo luo", 25, 34.47f, 100.24f},
    {"玉树", "yu shu", 25, 33.00f, 97.01f},
    {"海西", "hai xi", 25, 37.38f, 97.37f},
    // 宁夏
    {"银川", "yin chuan", 26, 38.49f, 106.23f},
    {"石嘴山", "shi zui shan", 26, 39.01f, 106.38f},
    {"吴忠", "wu zhong", 26, 37.99f, 106.20f},
    {"固原", "gu yuan", 26, 36.02f, 106.24f},
    {"中卫", "zhong wei", 26, 37.51f, 105.19f},
    // 新疆
    {"乌鲁木齐", "wu lu mu qi", 27, 43.83f, 87.62f},
    {"克拉玛依", "ke la ma yi", 27, 45.58f, 84.89f},
    {"吐鲁番", "tu lu fan", 27, 42.95f, 89.19f},
    {"哈密", "ha mi", 27, 42.83f, 93.51f},
    {"昌吉", "chang ji", 27, 44.01f, 87.31f},
    {"博尔塔拉", "bo er ta la", 27, 44.91f, 82.07f},
    {"巴音郭楞", "ba yin guo leng", 27, 41.76f, 86.15f},
    {"阿克苏", "a ke su", 27, 41.17f, 80.26f},
    {"克孜勒苏", "ke zi le su", 27, 39.71f, 76.17f},
    {"喀什", "ka shi", 27, 39.47f, 75.99f},
    {"和田", "he tian", 27, 37.11f, 79.92f},
    {"伊犁", "yi li", 27, 43.92f, 81.32f},
    {"塔城", "ta cheng", 27, 46.75f, 82.99f},
    {"阿勒泰", "a lei tai", 27, 47.85f, 88.14f},
    {"石河子", "shi he zi", 27, 44.31f, 86.04f},
    {"阿拉尔", "a la er", 27, 40.54f, 81.29f},
    {"图木舒克", "tu mu shu ke", 27, 39.87f, 79.07f},
    {"五家渠", "wu jia qu", 27, 44.17f, 87.54f},
    // 港澳台
    {"香港", "xiang gang", 28, 22.32f, 114.17f},
    {"澳门", "ao men", 28, 22.20f, 113.54f},
    {"台北", "tai bei", 28, 25.03f, 121.57f},
    {"新北", "xin bei", 28, 25.01f, 121.47f},
    {"桃园", "tao yuan", 28, 24.99f, 121.31f},
    {"台中", "tai zhong", 28, 24.15f, 120.67f},
    {"台南", "tai nan", 28, 22.99f, 120.21f},
    {"高雄", "gao xiong", 28, 22.63f, 120.30f},
};

const int N = sizeof(LIST) / sizeof(LIST[0]);

const char* const HOT_NAMES[] = {
    "北京", "上海", "广州", "深圳", "杭州", "成都", "重庆", "武汉",
    "南京", "苏州", "西安", "天津", "郑州", "长沙", "青岛", "宁波",
    "东莞", "合肥", "佛山", "沈阳",
};
const int HOT_N = sizeof(HOT_NAMES) / sizeof(HOT_NAMES[0]);
uint16_t HOT_IDX[sizeof(HOT_NAMES) / sizeof(HOT_NAMES[0])];

static void catPy(const char* py, char* out, size_t n) {
  size_t i = 0;
  for (const char* p = py; *p && i + 1 < n; ++p) {
    if (*p == ' ') continue;
    out[i++] = *p;
  }
  out[i] = 0;
}

static void firstLetters(const char* py, char* out, size_t n) {
  size_t i = 0;
  bool start = true;
  for (const char* p = py; *p && i + 1 < n; ++p) {
    if (*p == ' ') {
      start = true;
      continue;
    }
    if (start) {
      out[i++] = *p;
      start = false;
    }
  }
  out[i] = 0;
}

static void lowerCopy(const char* s, char* out, size_t n) {
  size_t i = 0;
  for (; s[i] && i + 1 < n; ++i) {
    const unsigned char c = (unsigned char)s[i];
    out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
  }
  out[i] = 0;
}

bool pyMatch(const Rec& c, const char* q) {
  if (!q || !q[0]) return true;
  char ql[24];
  lowerCopy(q, ql, sizeof(ql));
  // 丢掉空格,键盘偶尔会打出来
  char qn[24];
  catPy(ql, qn, sizeof(qn));
  if (!qn[0]) return true;
  const size_t qlN = strlen(qn);
  char cat[48];
  catPy(c.py, cat, sizeof(cat));
  if (strncmp(cat, qn, qlN) == 0) return true;
  char ab[16];
  firstLetters(c.py, ab, sizeof(ab));
  return strncmp(ab, qn, qlN) == 0;
}

int findName(const char* name) {
  if (!name || !name[0]) return -1;
  for (int i = 0; i < N; ++i) {
    if (strcmp(LIST[i].name, name) == 0) return i;
  }
  return -1;
}

int findLoose(const char* s) {
  if (!s || !s[0]) return -1;
  int i = findName(s);
  if (i >= 0) return i;

  // 去末尾「市」
  char buf[40];
  lowerCopy(s, buf, sizeof(buf));
  size_t n = strlen(buf);
  // UTF-8 市 = E5 B8 82
  if (n >= 3 && (unsigned char)buf[n - 3] == 0xE5 &&
      (unsigned char)buf[n - 2] == 0xB8 && (unsigned char)buf[n - 1] == 0x82) {
    buf[n - 3] = 0;
    i = findName(buf);
    if (i >= 0) return i;
  }

  // ip-api 偶尔给「巴音郭楞蒙古自治州」这类长名,用地名录当前缀对上
  {
    const size_t sl = strlen(buf);
    for (int k = 0; k < N; ++k) {
      const size_t ln = strlen(LIST[k].name);
      if (ln >= 6 && sl >= ln && strncmp(buf, LIST[k].name, ln) == 0) return k;
    }
  }

  // 英文/拼音:去掉 " shi" 再和去空格拼音比
  char pyq[40];
  catPy(buf, pyq, sizeof(pyq));
  n = strlen(pyq);
  if (n > 3 && strcmp(pyq + n - 3, "shi") == 0) pyq[n - 3] = 0;
  if (pyq[0]) {
    for (int k = 0; k < N; ++k) {
      char cat[48];
      catPy(LIST[k].py, cat, sizeof(cat));
      if (strcmp(cat, pyq) == 0) return k;
    }
  }
  return -1;
}

int matchCount(const char* q) {
  int n = 0;
  for (int i = 0; i < N; ++i)
    if (pyMatch(LIST[i], q)) ++n;
  return n;
}

int matchNth(const char* q, int n) {
  int seen = 0;
  for (int i = 0; i < N; ++i) {
    if (!pyMatch(LIST[i], q)) continue;
    if (seen == n) return i;
    ++seen;
  }
  return -1;
}

int provCount(uint8_t prov) {
  int n = 0;
  for (int i = 0; i < N; ++i)
    if (LIST[i].prov == prov) ++n;
  return n;
}

int provNth(uint8_t prov, int n) {
  int seen = 0;
  for (int i = 0; i < N; ++i) {
    if (LIST[i].prov != prov) continue;
    if (seen == n) return i;
    ++seen;
  }
  return -1;
}

void init() {
  for (int i = 0; i < HOT_N; ++i) {
    const int k = findName(HOT_NAMES[i]);
    HOT_IDX[i] = (uint16_t)(k < 0 ? 0 : k);
  }
}

}  // namespace Cities
