"""Each cell of localization/NebulaText.tsv read against the language of its column (tools/nebula_text.py --check runs it).

    python tools/nebula_text_languages.py     prints every finding of the whole table; exit 1 when there is one

A finding is one of:
  script     a character of another writing system than the column's: CJK, kana or Hangul in a Latin column; Hangul
             outside KrKorean; kana outside JpJapanese; a Simplified-only character in TwTradChinese or a
             Traditional-only one in CnSimpChinese (the common pairs below, each character used in one form only);
  arguments  a cell whose arguments ({0}, {name}, %s, %d) are not those of its UsEnglish cell;
  english    a cell equal to its UsEnglish cell in a language whose other columns translate it, unless the text is one
             the languages share (INTENDED: names, a format's name, OK where a language says OK).
An empty cell is the language's fallback (NebulaText.cpp) and is not read.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLE = os.path.join(ROOT, 'localization', 'NebulaText.tsv')

LATIN = {'UsEnglish', 'EuEnglish', 'UsFrench', 'EuFrench', 'UsSpanish', 'EuSpanish', 'EuGerman', 'EuItalian', 'EuDutch',
         'UsPortuguese'}
ENGLISH = {'UsEnglish', 'EuEnglish'}

# Simplified -> Traditional, for characters written one way in each script only (none is also a Traditional character
# of another word, as 后, 里, 面, 干, 只, 台 and 发 are left out).
PAIRS = ('这這 个個 们們 时時 来來 说說 国國 会會 对對 后後 进進 开開 关關 动動 问問 题題 现現 还還 没沒 样樣 经經 过過 '
         '让讓 应應 设設 档檔 载載 选選 择擇 项項 线線 网網 络絡 页頁 输輸 认認 错錯 误誤 数數 据據 储儲 读讀 写寫 删刪 '
         '导導 无無 处處 码碼 语語 号號 级級 种種 类類 显顯 图圖 资資 讯訊 帐帳 户戶 务務 机機 体體 视視 频頻 声聲 画畫 '
         '质質 节節 镜鏡 头頭 盘盤 离離 连連 继繼 续續 结結 换換 转轉 传傳 键鍵 钮鈕 标標 单單 双雙 简簡 东東 车車 马馬 '
         '鸟鳥 鱼魚 龙龍 门門 间間 闭閉 闪閃 记記 录錄 订訂 计計 许許 论論 识識 试試 话話 该該 调調 请請 购購 买買 卖賣 '
         '费費 赛賽 达達 运運 远遠 边邊 适適 锁鎖 钟鐘 银銀 长長 广廣 库庫 张張 弹彈 归歸 当當 惯慣 态態 总總 杂雜 观觀 '
         '览覽 觉覺 亲親 见見 规規 触觸 训訓 议議 讲講 证證 评評 词詞 译譯 详詳 谢謝 负負 财財 责責 败敗 货貨 贴貼 贵貴 '
         '赏賞 赢贏 跃躍 轻輕 较較 辑輯 办辦 迟遲 违違 迹跡 递遞 逻邏 邮郵 释釋 针針 钱錢 铁鐵 链鏈 闻聞 队隊 阶階 际際 '
         '陆陸 险險 随隨 隐隱 难難 静靜 顶頂 顺順 须須 预預 领領 颜顏 风風 飞飛 饰飾 馆館 驱驅 验驗 黄黃 点點 齐齊 夺奪 '
         '这這 们們 备備 装裝 启啟 动動 务務 组組 织織 纹紋 络絡 绘繪 给給 缓緩 编編 缩縮 网網 罗羅 盖蓋 获獲 虚虛 补補 '
         '袭襲 见見 览覽 计計 订訂 认認 讨討 让讓 记記 访訪 设設 许許 诉訴 诊診 词詞 译譯 试試 诚誠 话話 询詢 该該 详詳 '
         '语語 误誤 说說 请請 诸諸 读讀 谁誰 调調 谈談 谋謀 谐諧 谢謝 谱譜 贝貝 贝貝 购購 贯貫 贺賀 赖賴 赠贈 车車 轨軌 '
         '软軟 转轉 轮輪 软軟 载載 辅輔 辆輛 输輸 辖轄 边邊 达達 迁遷 过過 运運 还還 进進 远遠 违違 连連 迟遲 选選 递遞 '
         '钥鑰 铃鈴 铺鋪 链鏈 销銷 锁鎖 锐銳 错錯 锦錦 键鍵 镇鎮 门門 闪閃 闭閉 闲閒 间間 阅閱 阔闊 队隊 阳陽 阴陰 阵陣 '
         '际際 陆陸 陈陳 险險 隐隱 难難 电電 灵靈 页頁 顶頂 项項 顺順 须須 顾顧 预預 领領 频頻 题題 颜顏 额額 风風 飞飛 '
         '饭飯 馆館 驱驅 验驗 骑騎 鲜鮮 鸡雞 麦麥 黑黑 齿齒 万萬 与與 专專 业業 丢丟 两兩 严嚴 丰豐 为為 丽麗 举舉 义義 '
         '乐樂 乡鄉 书書 买買 乱亂 争爭 亏虧 产產 亿億 仅僅 从從 仓倉 价價 众眾 优優 伤傷 传傳 侧側 债債 储儲 儿兒 兑兌 '
         '兴興 军軍 农農 冻凍 净淨 减減 凤鳳 击擊 刘劉 则則 刚剛 创創 别別 刹剎 剧劇 劝勸 办辦 务務 动動 劳勞 势勢 勋勳 '
         '区區 医醫 华華 协協 单單 卖賣 卫衛 历歷 压壓 厅廳 县縣 参參 双雙 变變 叠疊 号號 叹嘆 吗嗎 启啟 员員 响響 哑啞 '
         '围圍 园園 图圖 场場 坏壞 块塊 坚堅 坛壇 垄壟 处處 备備 复復 头頭 夹夾 夺奪 奋奮 奖獎 妇婦 学學 宁寧 宝寶 实實 '
         '宠寵 审審 宪憲 宫宮 宽寬 宾賓 对對 寻尋 导導 将將 尔爾 尘塵 尝嘗 层層 属屬 岁歲 岛島 币幣 师師 带帶 帮幫 广廣 '
         '庄莊 庆慶 库庫 应應 废廢 开開 异異 弃棄 张張 弹彈 强強 归歸 录錄 彻徹 忆憶 态態 怀懷 总總 恋戀 恶惡 恼惱 '
         '悬懸 惊驚 惧懼 惯慣 愿願 戏戲 战戰 户戶 扩擴 扫掃 扬揚 护護 报報 担擔 拥擁 择擇 挂掛 挡擋 挥揮 损損 换換 据據 '
         '无無 旧舊 时時 显顯 晓曉 暂暫 术術 机機 杀殺 杂雜 权權 条條 来來 极極 构構 枪槍 标標 样樣 树樹 桥橋 档檔 梦夢 '
         '检檢 欢歡 毕畢 气氣 汇匯 汉漢 沟溝 没沒 泪淚 泽澤 洁潔 浅淺 测測 济濟 浏瀏 涂塗 温溫 湾灣 满滿 滚滾 灭滅 灯燈 '
         '灾災 炉爐 点點 炼煉 烟煙 热熱 爱愛 牵牽 犹猶 状狀 独獨 狮獅 猎獵 猫貓 献獻 环環 现現 琐瑣 电電 画畫 畅暢 疗療 '
         '盏盞 监監 盘盤 着著 矿礦 码碼 础礎 确確 礼禮 祸禍 离離 种種 积積 称稱 稳穩 穷窮 窃竊 竞競 笔筆 笼籠 筑築 签簽 '
         '简簡 类類 粮糧 紧緊 纠糾 红紅 约約 级級 纪紀 纯純 纳納 纵縱 纸紙 线線 练練 组組 细細 终終 经經 结結 绕繞 绘繪 '
         '给給 络絡 统統 继繼 绩績 续續 维維 综綜 绿綠 缆纜 缘緣 编編 缩縮 罚罰 罢罷 网網 职職 联聯 聪聰 肃肅 胜勝 胆膽 '
         '脑腦 脚腳 脱脫 艺藝 节節 范範 荐薦 药藥 获獲 莱萊 营營 虽雖 虫蟲 蚀蝕 补補 表錶 袄襖 装裝 观觀 规規 视視 览覽 '
         '觉覺 触觸 誉譽 计計 订訂 认認 讨討 让讓 训訓 议議 记記 讲講 许許 论論 设設 访訪 证證 评評 识識 诉訴 译譯 试試 '
         '诗詩 询詢 详詳 语語 误誤 说說 课課 谁誰 调調 谢謝 谨謹 贝貝 负負 贡貢 财財 责責 败敗 货貨 质質 购購 贯貫 贴貼 '
         '贵貴 费費 贺賀 资資 赏賞 赛賽 赢贏 赵趙 趋趨 跃躍 践踐 踪蹤 车車 轨軌 轮輪 软軟 轻輕 载載 较較 辅輔 辑輯 输輸 '
         '辞辭 边邊 辽遼 达達 迁遷 迈邁 过過 运運 还還 这這 进進 远遠 违違 连連 迟遲 适適 选選 逊遜 递遞 逻邏 遗遺 邓鄧 '
         '邮郵 邻鄰 酱醬 释釋 里裡 针針 钓釣 钟鐘 钢鋼 钥鑰 钱錢 铁鐵 银銀 铺鋪 链鏈 销銷 锁鎖 锅鍋 错錯 键鍵 镜鏡 长長 '
         '门門 闪閃 闭閉 问問 闲閒 间間 闹鬧 闻聞 阅閱 队隊 阶階 际際 陆陸 陈陳 险險 随隨 隐隱 难難 雾霧 静靜 顶頂 项項 '
         '顺順 须須 顾顧 顿頓 预預 领領 频頻 题題 颜顏 额額 风風 飞飛 饭飯 饮飲 饰飾 饱飽 馆館 驱驅 驶駛 验驗 骑騎 骤驟 '
         '鱼魚 鸟鳥 鸡雞 麦麥 黄黃 齐齊 龙龍 龟龜')
SIMPLIFIED_ONLY = {}
TRADITIONAL_ONLY = {}
for pair in PAIRS.split():
    if len(pair) == 2 and pair[0] != pair[1] and pair[0] not in ('里', '表'):
        SIMPLIFIED_ONLY[pair[0]] = pair[1]
        TRADITIONAL_ONLY[pair[1]] = pair[0]

# Texts every language may keep as UsEnglish writes them.
INTENDED = {'OK', 'Mii', 'Mii Studio', 'Mii Creator', 'Nebula', 'Wii', 'Wii U', 'DS', '3DS', 'Switch', 'LumaCore', 'LumaLink',
            'Vulkan', 'Direct3D 12', 'D3D12', 'Metal', 'OpenGL', 'Android', 'iOS', 'macOS', 'Linux', 'Windows', 'Steam',
            'Gamepad', 'AndroidX', 'Kotlin', 'SDL', 'DLSS', 'FSR', 'XeSS', 'HDR', 'FPS', 'V-Sync', 'MSAA', 'SSAO', 'GTAO',
            'ISO', 'RVZ', 'WIA', 'WBFS', 'CISO', 'GCZ', 'ZIP', 'Bluetooth', 'USB', 'Wi-Fi', 'Super Mario Galaxy', 'Mario',
            'Luigi', 'Luma', 'Rosalina', 'Bowser', 'Bowser Jr.', 'Toad', 'Yoshi', 'Peach', 'Lubba', 'Polari', 'Gearmo',
            'Hungry Luma', 'Star Bits', 'Nunchuk', 'Wii Remote', 'Joy-Con', 'DualSense', 'DualShock 4', 'Xbox', 'Pro Controller',
            'Mod', 'Mods', 'Menu', 'Info', 'Online', 'Offline', 'Normal', 'Auto', 'Debug', 'Log', 'Ghost', 'Replay',
            'Version', 'Volume', 'Original', 'Radio', 'Action', 'Animation', 'Animationen', 'Format', 'Mode', 'Audio', 'Video',
            'Stereo', 'Mono', 'Surround', 'Chat', 'Mail', 'Online', 'Pixel', 'Pixels', 'Zoom', 'Bloom', 'Tutorial', 'Tutorials',
            'Shader', 'Shaders', 'Pack', 'Packs', 'Cache', 'Status', 'Start', 'Stop', 'Pause', 'Reset', 'Turbo', 'Score',
            'Level', 'Levels', 'Galaxy', 'Galaxies', 'Planet', 'Station', 'Comet', 'Fluzzard', 'Ice', 'Spin', 'Sprint',
            'Speedrun', 'Timer', 'Splits', 'LiveSplit', 'Controller', 'Controllers', 'Touch', 'Gyro', 'Motion', 'Pointer',
            # key caps as Japanese, Korean, Chinese and Latin American keyboards print them
            'Enter', 'Backspace', 'Esc', 'Tab', 'Shift', 'Ctrl', 'Alt', 'Caps Lock', 'Home', 'End', 'Insert', 'Delete',
            'Page Up', 'Page Down', 'Mouse'}
# A language's own name, as a language list shows it in every column.
ENDONYMS = {'English', 'Français', 'Español', 'Deutsch', 'Italiano', 'Nederlands', 'Português', '日本語', '한국어', '中文',
            '简体中文', '繁體中文', '繁体中文'}
# Findings in rows still to be corrected, told to whoever owns them: the check passes them until they are fixed, and
# fails if one of them is fixed and still listed (remove it here then).
REPORTED = {
    'english: Validator.L213 KrKorean',
}
# Columns of one language, or of one writing system: a cell left as UsEnglish where another of its group translates
# it. German, Italian, Dutch and Portuguese have one column each and share many words with English (Version, Name,
# Japan): they are not read for this.
GROUPS = {'French': ('UsFrench', 'EuFrench'), 'Spanish': ('UsSpanish', 'EuSpanish'),
          'CJK': ('JpJapanese', 'KrKorean', 'CnSimpChinese', 'TwTradChinese')}
GROUP_OF = {column: name for name, columns in GROUPS.items() for column in columns}
UNITS = re.compile(r'^[\W\d_]*(?:B|KB|MB|GB|ms|s|Hz|FPS|nits|x|px|dB|%)?[\W\d_]*$')
SHARED = re.compile(r'^[\W\d_]*$|^[A-Z0-9][A-Z0-9 .+/:_-]*$')  # no letters, or a code (RMGE01, BC7, F11)

ARGUMENT = re.compile(r'\{[A-Za-z0-9_]*\}|%[-+ #0]*[0-9]*(?:\.[0-9]+)?(?:ll|l|z|h)?[sdufxXcp]|%[0-9]+\$[sd]')


def kind(ch):
    code = ord(ch)
    if 0x3040 <= code <= 0x30FF or 0x31F0 <= code <= 0x31FF or 0xFF66 <= code <= 0xFF9F:
        return 'kana'
    if 0xAC00 <= code <= 0xD7AF or 0x1100 <= code <= 0x11FF or 0x3130 <= code <= 0x318F:
        return 'hangul'
    if 0x4E00 <= code <= 0x9FFF or 0x3400 <= code <= 0x4DBF or 0xF900 <= code <= 0xFAFF:
        return 'han'
    return None


def unescape(cell):
    return cell.replace('\\n', '\n').replace('\\\\', '\\')


def script_findings(column, text):
    found = []
    kinds = {}
    for ch in text:
        k = kind(ch)
        if k:
            kinds.setdefault(k, []).append(ch)
    if column in LATIN:
        for k, chars in kinds.items():
            found.append('%s in a Latin column: %s' % (k, ''.join(sorted(set(chars)))))
        return found
    if 'hangul' in kinds and column != 'KrKorean':
        found.append('Hangul: %s' % ''.join(sorted(set(kinds['hangul']))))
    if 'kana' in kinds and column != 'JpJapanese':
        found.append('kana: %s' % ''.join(sorted(set(kinds['kana']))))
    if column == 'TwTradChinese':
        wrong = sorted({ch for ch in text if ch in SIMPLIFIED_ONLY})
        if wrong:
            found.append('Simplified: %s' % ' '.join('%s (%s)' % (ch, SIMPLIFIED_ONLY[ch]) for ch in wrong))
    if column == 'CnSimpChinese':
        wrong = sorted({ch for ch in text if ch in TRADITIONAL_ONLY})
        if wrong:
            found.append('Traditional: %s' % ' '.join('%s (%s)' % (ch, TRADITIONAL_ONLY[ch]) for ch in wrong))
    return found


def findings(path=TABLE):
    with open(path, encoding='utf-8') as f:
        lines = [line.rstrip('\r\n').split('\t') for line in f if line.strip()]
    header = lines[0]
    out = []
    for cells in lines[1:]:
        key, texts = cells[0], [unescape(c) for c in cells[1:]] + [''] * (len(header) - len(cells))
        cols = dict(zip(header[1:], texts))
        english = cols.get('UsEnglish', '')
        wanted = sorted(set(ARGUMENT.findall(english)))
        bare = ARGUMENT.sub('', english).strip()
        shared = bare in INTENDED or SHARED.match(bare) or UNITS.match(bare) or not re.search('[A-Za-z]', english)
        for column, text in cols.items():
            if not text:
                continue
            if text != english and text not in ENDONYMS:  # a name kept as written: 日本語 in every column
                for finding in script_findings(column, text):
                    out.append('script: %s %s: %s' % (key, column, finding))
            got = sorted(set(ARGUMENT.findall(text)))
            if column != 'UsEnglish' and english and got != wanted:
                out.append('arguments: %s %s: %s, UsEnglish %s' % (key, column, ' '.join(got) or 'none', ' '.join(wanted) or 'none'))
            group = GROUP_OF.get(column)
            if group and text == english and not shared:
                translated = [c for c in GROUPS[group] if c != column and cols.get(c) and cols[c] != english]
                if translated:
                    out.append('english: %s %s: "%s" (translated in %s)' % (key, column, english[:60].replace('\n', '\\n'),
                                                                           ', '.join(translated)))
    return out


def failing(found):
    """The findings that fail the check: those not in REPORTED, and each REPORTED one that is no longer found."""
    heads = {f.split(':', 2)[0] + ':' + f.split(':', 2)[1] for f in found}
    return ([f for f in found if f.split(':', 2)[0] + ':' + f.split(':', 2)[1] not in REPORTED]
            + ['fixed, still listed in REPORTED: %s' % r for r in sorted(REPORTED - heads)])


def main():
    found = findings()
    for line in found:
        mark = ' (reported)' if line.split(':', 2)[0] + ':' + line.split(':', 2)[1] in REPORTED else ''
        print((line + mark).encode('ascii', 'backslashreplace').decode() if '--ascii' in sys.argv else line + mark)
    stale = [f for f in failing(found) if f.startswith('fixed, ')]
    for line in stale:
        print(line)
    return 1 if failing(found) else 0


if __name__ == '__main__':
    sys.exit(main())
