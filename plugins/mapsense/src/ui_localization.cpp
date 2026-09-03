#include "ui_localization.hpp"

#include <D2RLPlugin/api.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace RuffnecKk::MapSense {
namespace {

using Translation = std::array<const char*, UiLanguageCount>;

// Column order follows UiLanguage. These strings belong to MapSense and are
// therefore embedded in the plugin; D2R remains authoritative for all game
// content names. Spanish wording is deliberately neutral for esES and esMX.
constexpr std::array<Translation, UiTextCount> TextCatalog{{
    {{"Open", "開啟", "Öffnen", "Abrir", "Ouvrir", "Apri", "열기", "Otwórz", "開く", "Abrir", "Открыть", "打开"}},
    {{"Open (OFF)", "開啟（關閉）", "Öffnen (AUS)", "Abrir (DESACTIVADO)", "Ouvrir (DÉSACTIVÉ)", "Apri (DISATTIVATO)", "열기 (꺼짐)", "Otwórz (WYŁ.)", "開く（オフ）", "Abrir (DESATIVADO)", "Открыть (ВЫКЛ.)", "打开（关闭）"}},
    {{"Collapse", "收合", "Einklappen", "Contraer", "Réduire", "Comprimi", "접기", "Zwiń", "折りたたむ", "Recolher", "Свернуть", "收起"}},
    {{"Enable MapSense", "啟用 MapSense", "MapSense aktivieren", "Activar MapSense", "Activer MapSense", "Attiva MapSense", "MapSense 활성화", "Włącz MapSense", "MapSenseを有効化", "Ativar MapSense", "Включить MapSense", "启用 MapSense"}},
    {{"Appearance", "外觀", "Darstellung", "Apariencia", "Apparence", "Aspetto", "모양", "Wygląd", "外観", "Aparência", "Внешний вид", "外观"}},
    {{"Menu Theme", "選單主題", "Menüdesign", "Tema del menú", "Thème du menu", "Tema del menu", "메뉴 테마", "Motyw menu", "メニューテーマ", "Tema do menu", "Тема меню", "菜单主题"}},
    {{"Map & Reveal", "地圖與揭示", "Karte & Aufdecken", "Mapa y revelado", "Carte et révélation", "Mappa e rivelazione", "지도 및 공개", "Mapa i odkrywanie", "マップと表示", "Mapa e revelação", "Карта и открытие", "地图与揭示"}},
    {{"Reveal Map", "揭示地圖", "Karte aufdecken", "Revelar mapa", "Révéler la carte", "Rivela mappa", "지도 공개", "Odkryj mapę", "マップを表示", "Revelar mapa", "Открыть карту", "揭示地图"}},
    {{"Additions Opacity", "附加標記透明度", "Deckkraft der Ergänzungen", "Opacidad de elementos añadidos", "Opacité des ajouts", "Opacità degli elementi aggiunti", "추가 요소 불투명도", "Krycie dodatków", "追加表示の不透明度", "Opacidade dos elementos adicionais", "Прозрачность дополнений", "附加标记不透明度"}},
    {{"Monsters", "怪物", "Monster", "Monstruos", "Monstres", "Mostri", "괴물", "Potwory", "モンスター", "Monstros", "Монстры", "怪物"}},
    {{"Show Monsters", "顯示怪物", "Monster anzeigen", "Mostrar monstruos", "Afficher les monstres", "Mostra mostri", "괴물 표시", "Pokaż potwory", "モンスターを表示", "Mostrar monstros", "Показывать монстров", "显示怪物"}},
    {{"Normal", "普通", "Normal", "Normal", "Normal", "Normale", "일반", "Zwykły", "通常", "Normal", "Обычный", "普通"}},
    {{"Minion", "隨從", "Diener", "Esbirro", "Serviteur", "Servitore", "하수인", "Sługa", "ミニオン", "Lacaio", "Миньон", "仆从"}},
    {{"Champion", "勇士", "Champion", "Campeón", "Champion", "Campione", "용사", "Czempion", "チャンピオン", "Campeão", "Чемпион", "勇士"}},
    {{"Unique", "獨特", "Einzigartig", "Único", "Unique", "Unico", "고유", "Unikalny", "ユニーク", "Único", "Уникальный", "独特"}},
    {{"Super Unique / Boss", "超級獨特／首領", "Superunikat / Boss", "Súper único / Jefe", "Super unique / Boss", "Super unico / Boss", "슈퍼 유니크 / 우두머리", "Superunikat / Boss", "スーパーユニーク / ボス", "Superúnico / Chefe", "Суперуникальный / Босс", "超级独特／首领"}},
    {{"Immunities", "免疫", "Immunitäten", "Inmunidades", "Immunités", "Immunità", "면역", "Odporności", "無効属性", "Imunidades", "Иммунитеты", "免疫"}},
    {{"Show Immunities", "顯示免疫", "Immunitäten anzeigen", "Mostrar inmunidades", "Afficher les immunités", "Mostra immunità", "면역 표시", "Pokaż odporności", "無効属性を表示", "Mostrar imunidades", "Показывать иммунитеты", "显示免疫"}},
    {{"Indicator Size", "指示器大小", "Indikatorgröße", "Tamaño del indicador", "Taille de l’indicateur", "Dimensione indicatore", "표시기 크기", "Rozmiar wskaźnika", "インジケーターサイズ", "Tamanho do indicador", "Размер индикатора", "指示器大小"}},
    {{"Halo Thickness", "光環粗細", "Halo-Stärke", "Grosor del halo", "Épaisseur du halo", "Spessore alone", "후광 두께", "Grubość obwódki", "ハローの太さ", "Espessura do halo", "Толщина ореола", "光环粗细"}},
    {{"Colors", "顏色", "Farben", "Colores", "Couleurs", "Colori", "색상", "Kolory", "色", "Cores", "Цвета", "颜色"}},
    {{"Missiles", "投射物", "Projektile", "Proyectiles", "Projectiles", "Proiettili", "투사체", "Pociski", "飛翔物", "Projéteis", "Снаряды", "投射物"}},
    {{"Show Missiles", "顯示投射物", "Projektile anzeigen", "Mostrar proyectiles", "Afficher les projectiles", "Mostra proiettili", "투사체 표시", "Pokaż pociski", "飛翔物を表示", "Mostrar projéteis", "Показывать снаряды", "显示投射物"}},
    {{"Fire", "火焰", "Feuer", "Fuego", "Feu", "Fuoco", "화염", "Ogień", "火炎", "Fogo", "Огонь", "火焰"}},
    {{"Cold", "冰寒", "Kälte", "Frío", "Froid", "Freddo", "냉기", "Zimno", "冷気", "Gelo", "Холод", "冰寒"}},
    {{"Cold / Ice", "冰寒／冰霜", "Kälte / Eis", "Frío / Hielo", "Froid / Glace", "Freddo / Ghiaccio", "냉기 / 얼음", "Zimno / Lód", "冷気 / 氷", "Gelo / Gelo intenso", "Холод / Лёд", "冰寒／冰霜"}},
    {{"Lightning", "閃電", "Blitz", "Rayo", "Foudre", "Fulmine", "번개", "Błyskawice", "稲妻", "Raio", "Молния", "闪电"}},
    {{"Poison", "毒素", "Gift", "Veneno", "Poison", "Veleno", "독", "Trucizna", "毒", "Veneno", "Яд", "毒素"}},
    {{"Physical", "物理", "Physisch", "Físico", "Physique", "Fisico", "물리", "Fizyczne", "物理", "Físico", "Физический", "物理"}},
    {{"Magic", "魔法", "Magie", "Magia", "Magie", "Magia", "마법", "Magia", "魔法", "Magia", "Магия", "魔法"}},
    {{"Objects", "物件", "Objekte", "Objetos", "Objets", "Oggetti", "오브젝트", "Obiekty", "オブジェクト", "Objetos", "Объекты", "物件"}},
    {{"Show Automap Objects", "顯示自動地圖物件", "Automap-Objekte anzeigen", "Mostrar objetos del mapa automático", "Afficher les objets de l’automappe", "Mostra oggetti della mappa automatica", "자동 지도 오브젝트 표시", "Pokaż obiekty automapy", "オートマップのオブジェクトを表示", "Mostrar objetos do mapa automático", "Показывать объекты автокарты", "显示自动地图物件"}},
    {{"Exit Labels", "出口標籤", "Ausgangsbeschriftungen", "Etiquetas de salidas", "Étiquettes des sorties", "Etichette uscite", "출구 이름", "Etykiety wyjść", "出口ラベル", "Rótulos de saídas", "Подписи выходов", "出口标签"}},
    {{"Show Exit Labels", "顯示出口標籤", "Ausgangsbeschriftungen anzeigen", "Mostrar etiquetas de salidas", "Afficher les étiquettes des sorties", "Mostra etichette uscite", "출구 이름 표시", "Pokaż etykiety wyjść", "出口ラベルを表示", "Mostrar rótulos de saídas", "Показывать подписи выходов", "显示出口标签"}},
    {{"Waypoint Labels", "傳送站標籤", "Wegpunktbeschriftungen", "Etiquetas de transportadores", "Étiquettes des relais", "Etichette crocevia", "순간이동진 이름", "Etykiety punktów nawigacyjnych", "ウェイポイントラベル", "Rótulos de pontos de senda", "Подписи точек перехода", "传送点标签"}},
    {{"Show Waypoint Labels", "顯示傳送站標籤", "Wegpunktbeschriftungen anzeigen", "Mostrar etiquetas de transportadores", "Afficher les étiquettes des relais", "Mostra etichette crocevia", "순간이동진 이름 표시", "Pokaż etykiety punktów nawigacyjnych", "ウェイポイントラベルを表示", "Mostrar rótulos de pontos de senda", "Показывать подписи точек перехода", "显示传送点标签"}},
    {{"Shrine Labels", "神殿標籤", "Schreinbeschriftungen", "Etiquetas de santuarios", "Étiquettes des sanctuaires", "Etichette santuari", "성소 이름", "Etykiety kapliczek", "祠ラベル", "Rótulos de santuários", "Подписи святилищ", "神殿标签"}},
    {{"Show Shrine Labels", "顯示神殿標籤", "Schreinbeschriftungen anzeigen", "Mostrar etiquetas de santuarios", "Afficher les étiquettes des sanctuaires", "Mostra etichette santuari", "성소 이름 표시", "Pokaż etykiety kapliczek", "祠ラベルを表示", "Mostrar rótulos de santuários", "Показывать подписи святилищ", "显示神殿标签"}},
    {{"Chests", "寶箱", "Truhen", "Cofres", "Coffres", "Forzieri", "상자", "Skrzynie", "宝箱", "Baús", "Сундуки", "宝箱"}},
    {{"Show Chests", "顯示寶箱", "Truhen anzeigen", "Mostrar cofres", "Afficher les coffres", "Mostra forzieri", "상자 표시", "Pokaż skrzynie", "宝箱を表示", "Mostrar baús", "Показывать сундуки", "显示宝箱"}},
    {{"Marker Size", "標記大小", "Markierungsgröße", "Tamaño del marcador", "Taille du marqueur", "Dimensione indicatore", "표식 크기", "Rozmiar znacznika", "マーカーサイズ", "Tamanho do marcador", "Размер маркера", "标记大小"}},
    {{"Locked Lock Color", "上鎖標記顏色", "Farbe für verschlossene Schlösser", "Color de cierre bloqueado", "Couleur du verrou fermé", "Colore serratura chiusa", "잠긴 자물쇠 색상", "Kolor zamkniętego zamka", "施錠マークの色", "Cor da trava fechada", "Цвет закрытого замка", "上锁标记颜色"}},
    {{"Trapped Lock Color", "陷阱標記顏色", "Farbe für Fallen-Schlösser", "Color de cierre con trampa", "Couleur du verrou piégé", "Colore serratura con trappola", "함정 자물쇠 색상", "Kolor zamka z pułapką", "罠マークの色", "Cor da trava com armadilha", "Цвет замка с ловушкой", "陷阱标记颜色"}},
    {{"Special Chests", "特殊寶箱", "Besondere Truhen", "Cofres especiales", "Coffres spéciaux", "Forzieri speciali", "특수 상자", "Specjalne skrzynie", "特殊な宝箱", "Baús especiais", "Особые сундуки", "特殊宝箱"}},
    {{"Show Special Chests", "顯示特殊寶箱", "Besondere Truhen anzeigen", "Mostrar cofres especiales", "Afficher les coffres spéciaux", "Mostra forzieri speciali", "특수 상자 표시", "Pokaż specjalne skrzynie", "特殊な宝箱を表示", "Mostrar baús especiais", "Показывать особые сундуки", "显示特殊宝箱"}},
    {{"Armor Racks", "護甲架", "Rüstungsständer", "Expositores de armaduras", "Râteliers d’armures", "Rastrelliere per armature", "방어구 거치대", "Stojaki na zbroje", "防具ラック", "Suportes de armadura", "Стойки для доспехов", "护甲架"}},
    {{"Show Armor Racks", "顯示護甲架", "Rüstungsständer anzeigen", "Mostrar expositores de armaduras", "Afficher les râteliers d’armures", "Mostra rastrelliere per armature", "방어구 거치대 표시", "Pokaż stojaki na zbroje", "防具ラックを表示", "Mostrar suportes de armadura", "Показывать стойки для доспехов", "显示护甲架"}},
    {{"Weapon Racks", "武器架", "Waffenständer", "Expositores de armas", "Râteliers d’armes", "Rastrelliere per armi", "무기 거치대", "Stojaki na broń", "武器ラック", "Suportes de armas", "Стойки для оружия", "武器架"}},
    {{"Show Weapon Racks", "顯示武器架", "Waffenständer anzeigen", "Mostrar expositores de armas", "Afficher les râteliers d’armes", "Mostra rastrelliere per armi", "무기 거치대 표시", "Pokaż stojaki na broń", "武器ラックを表示", "Mostrar suportes de armas", "Показывать стойки для оружия", "显示武器架"}},
    {{"Navigation", "導航", "Navigation", "Navegación", "Navigation", "Navigazione", "길찾기", "Nawigacja", "ナビゲーション", "Navegação", "Навигация", "导航"}},
    {{"Line Thickness", "線條粗細", "Linienstärke", "Grosor de línea", "Épaisseur de ligne", "Spessore linea", "선 두께", "Grubość linii", "線の太さ", "Espessura da linha", "Толщина линии", "线条粗细"}},
    {{"Waypoint", "傳送站", "Wegpunkt", "Transportador", "Relais", "Crocevia", "순간이동진", "Punkt nawigacyjny", "ウェイポイント", "Ponto de senda", "Точка перехода", "传送点"}},
    {{"Waypoint Line", "傳送站路線", "Wegpunktlinie", "Línea al transportador", "Ligne vers le relais", "Linea crocevia", "순간이동진 경로", "Linia do punktu nawigacyjnego", "ウェイポイント線", "Linha do ponto de senda", "Линия к точке перехода", "传送点路线"}},
    {{"Main Progression", "主要進度", "Hauptfortschritt", "Progresión principal", "Progression principale", "Progressione principale", "주요 진행", "Główny postęp", "メイン進行", "Progressão principal", "Основной путь", "主要进度"}},
    {{"Main Progression Line", "主要進度路線", "Hauptfortschrittslinie", "Línea de progresión principal", "Ligne de progression principale", "Linea progressione principale", "주요 진행 경로", "Linia głównego postępu", "メイン進行線", "Linha de progressão principal", "Линия основного пути", "主要进度路线"}},
    {{"Quest Targets", "任務目標", "Questziele", "Objetivos de misión", "Objectifs de quête", "Obiettivi missione", "퀘스트 목표", "Cele zadań", "クエスト目標", "Alvos de missão", "Цели заданий", "任务目标"}},
    {{"Quest Target Line", "任務目標路線", "Questziellinie", "Línea al objetivo de misión", "Ligne vers l’objectif de quête", "Linea obiettivo missione", "퀘스트 목표 경로", "Linia celu zadania", "クエスト目標線", "Linha ao alvo de missão", "Линия к цели задания", "任务目标路线"}},
    {{"Custom Levels", "自訂區域", "Eigene Gebiete", "Zonas personalizadas", "Zones personnalisées", "Aree personalizzate", "사용자 지정 지역", "Niestandardowe obszary", "カスタムエリア", "Áreas personalizadas", "Пользовательские области", "自定义区域"}},
    {{"Custom Level Lines", "自訂區域路線", "Linien zu eigenen Gebieten", "Líneas a zonas personalizadas", "Lignes vers les zones personnalisées", "Linee aree personalizzate", "사용자 지정 지역 경로", "Linie do niestandardowych obszarów", "カスタムエリア線", "Linhas para áreas personalizadas", "Линии к пользовательским областям", "自定义区域路线"}},
    {{"(to add more custom destinations, configure in TOML)", "（若要新增更多自訂目的地，請在 TOML 中設定）", "(weitere eigene Ziele in TOML konfigurieren)", "(para añadir más destinos personalizados, configúralos en TOML)", "(pour ajouter d’autres destinations personnalisées, configurez-les dans le TOML)", "(per aggiungere altre destinazioni personalizzate, configurale nel TOML)", "(사용자 지정 목적지를 더 추가하려면 TOML에서 설정하세요)", "(aby dodać więcej własnych celów, skonfiguruj je w TOML)", "（カスタム目的地を追加するにはTOMLで設定してください）", "(para adicionar mais destinos personalizados, configure-os no TOML)", "(чтобы добавить другие пользовательские цели, настройте их в TOML)", "（若要添加更多自定义目的地，请在 TOML 中设置）"}},
    {{"Line Color", "線條顏色", "Linienfarbe", "Color de línea", "Couleur de ligne", "Colore linea", "선 색상", "Kolor linii", "線の色", "Cor da linha", "Цвет линии", "线条颜色"}},
    {{"Shape", "形狀", "Form", "Forma", "Forme", "Forma", "모양", "Kształt", "形状", "Forma", "Форма", "形状"}},
    {{"Player Cross", "玩家十字", "Spielerkreuz", "Cruz de jugador", "Croix de joueur", "Croce giocatore", "플레이어 십자", "Krzyż gracza", "プレイヤークロス", "Cruz de jogador", "Крест игрока", "玩家十字"}},
    {{"Dot", "圓點", "Punkt", "Punto", "Point", "Punto", "점", "Kropka", "点", "Ponto", "Точка", "圆点"}},
    {{"Style", "樣式", "Stil", "Estilo", "Style", "Stile", "스타일", "Styl", "スタイル", "Estilo", "Стиль", "样式"}},
    {{"Colored i", "彩色 i", "Farbiges i", "i coloreada", "i coloré", "i colorata", "색상 i", "Kolorowe i", "色付き i", "i colorido", "Цветная i", "彩色 i"}},
    {{"Split Halo", "分段光環", "Geteilter Halo", "Halo dividido", "Halo divisé", "Alone diviso", "분할 후광", "Podzielona obwódka", "分割ハロー", "Halo dividido", "Разделённый ореол", "分段光环"}},
    {{"Color", "顏色", "Farbe", "Color", "Couleur", "Colore", "색상", "Kolor", "色", "Cor", "Цвет", "颜色"}},
    {{"Size", "大小", "Größe", "Tamaño", "Taille", "Dimensione", "크기", "Rozmiar", "サイズ", "Tamanho", "Размер", "大小"}},
    {{"Thickness", "粗細", "Stärke", "Grosor", "Épaisseur", "Spessore", "두께", "Grubość", "太さ", "Espessura", "Толщина", "粗细"}},
    {{"Names", "名稱", "Namen", "Nombres", "Noms", "Nomi", "이름", "Nazwy", "名前", "Nomes", "Имена", "名称"}},
    {{"Show Names", "顯示名稱", "Namen anzeigen", "Mostrar nombres", "Afficher les noms", "Mostra nomi", "이름 표시", "Pokaż nazwy", "名前を表示", "Mostrar nomes", "Показывать имена", "显示名称"}},
    {{"Name Size", "名稱大小", "Namensgröße", "Tamaño del nombre", "Taille du nom", "Dimensione nome", "이름 크기", "Rozmiar nazwy", "名前のサイズ", "Tamanho do nome", "Размер имени", "名称大小"}},
    {{"Name Color", "名稱顏色", "Namensfarbe", "Color del nombre", "Couleur du nom", "Colore nome", "이름 색상", "Kolor nazwy", "名前の色", "Cor do nome", "Цвет имени", "名称颜色"}},
    {{"Text Size", "文字大小", "Textgröße", "Tamaño del texto", "Taille du texte", "Dimensione testo", "텍스트 크기", "Rozmiar tekstu", "テキストサイズ", "Tamanho do texto", "Размер текста", "文字大小"}},
    {{"Text Color", "文字顏色", "Textfarbe", "Color del texto", "Couleur du texte", "Colore testo", "텍스트 색상", "Kolor tekstu", "テキストの色", "Cor do texto", "Цвет текста", "文字颜色"}},
    {{"Marker Color", "標記顏色", "Markierungsfarbe", "Color del marcador", "Couleur du marqueur", "Colore indicatore", "표식 색상", "Kolor znacznika", "マーカーの色", "Cor do marcador", "Цвет маркера", "标记颜色"}},
    {{"Sanctuary Gold", "聖休亞瑞之金", "Sanktuario-Gold", "Oro de Santuario", "Or de Sanctuaire", "Oro di Sanctuarium", "성역의 황금", "Złoto Sanktuarium", "サンクチュアリ・ゴールド", "Ouro de Santuário", "Золото Санктуария", "庇护之地金色"}},
    {{"Hellfire", "地獄火", "Höllenfeuer", "Fuego infernal", "Feu infernal", "Fuoco infernale", "지옥불", "Ogień piekielny", "業火", "Fogo infernal", "Адское пламя", "地狱火"}},
    {{"Horadric Sand", "赫拉迪姆之沙", "Horadrischer Sand", "Arena horádrica", "Sable horadrique", "Sabbia horadrica", "호라드림의 모래", "Horadrimski piasek", "ホラドリムの砂", "Areia horádrica", "Хорадрический песок", "赫拉迪姆之沙"}},
    {{"Arcane Sanctuary", "秘法聖殿", "Geheime Zuflucht", "Santuario Arcano", "Sanctuaire des Arcanes", "Santuario Arcano", "비전의 성역", "Tajemne Sanktuarium", "深遠なる聖域", "Santuário Arcano", "Магическое убежище", "神秘避难所"}},
    {{"Tristram Moon", "崔斯特姆之月", "Tristram-Mond", "Luna de Tristram", "Lune de Tristram", "Luna di Tristram", "트리스트럼의 달", "Księżyc Tristram", "トリストラムの月", "Lua de Tristram", "Луна Тристрама", "崔斯特姆之月"}},
    {{"Kurast Jade", "庫拉斯特翡翠", "Kurast-Jade", "Jade de Kurast", "Jade de Kurast", "Giada di Kurast", "쿠라스트 비취", "Jadeit Kurast", "クラストの翡翠", "Jade de Kurast", "Нефрит Кураста", "库拉斯特翡翠"}},
    {{"Necromancer Bone", "死靈法師之骨", "Totenbeschwörerknochen", "Hueso de nigromante", "Os de nécromancien", "Osso del negromante", "강령술사의 뼈", "Kość nekromanty", "ネクロマンサーの骨", "Osso de necromante", "Кость некроманта", "死灵法师之骨"}},
    {{"Harrogath Frost", "哈洛加斯冰霜", "Harrogath-Frost", "Escarcha de Harrogath", "Givre d’Harrogath", "Gelo di Harrogath", "하로가스의 서리", "Szron Harrogath", "ハロガスの霜", "Geada de Harrogath", "Мороз Харрогата", "哈洛加斯冰霜"}},
    {{"Blood Moor", "鮮血荒地", "Blutmoor", "Páramo Sangriento", "Lande sanglante", "Brughiera Insanguinata", "핏빛 황무지", "Krwawe Wrzosowisko", "血の荒野", "Charneca Sangrenta", "Кровавое болото", "鲜血荒地"}},
    {{"High Contrast", "高對比", "Hoher Kontrast", "Alto contraste", "Contraste élevé", "Contrasto elevato", "고대비", "Wysoki kontrast", "ハイコントラスト", "Alto contraste", "Высокая контрастность", "高对比"}},
}};

static_assert(TextCatalog.size() == UiTextCount);

std::atomic<UiLanguage> ActiveLanguage{UiLanguage::English};

[[nodiscard]] constexpr auto LanguageIndex(UiLanguage language) noexcept
        -> std::size_t {
    const auto index = static_cast<std::size_t>(language);
    return index < UiLanguageCount ? index : 0U;
}

[[nodiscard]] auto Utf8Valid(std::string_view text) noexcept -> bool {
    std::size_t index{};
    while (index < text.size()) {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7FU) {
            ++index;
            continue;
        }
        std::size_t continuationCount{};
        std::uint32_t value{};
        std::uint32_t minimum{};
        if ((first & 0xE0U) == 0xC0U) {
            continuationCount = 1U;
            value = first & 0x1FU;
            minimum = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U) {
            continuationCount = 2U;
            value = first & 0x0FU;
            minimum = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U) {
            continuationCount = 3U;
            value = first & 0x07U;
            minimum = 0x10000U;
        } else {
            return false;
        }
        if (index + continuationCount >= text.size()) return false;
        for (std::size_t part = 1U; part <= continuationCount; ++part) {
            const auto byte = static_cast<unsigned char>(text[index + part]);
            if ((byte & 0xC0U) != 0x80U) return false;
            value = (value << 6U) | (byte & 0x3FU);
        }
        if (value < minimum || value > 0x10FFFFU
            || (value >= 0xD800U && value <= 0xDFFFU)) {
            return false;
        }
        index += continuationCount + 1U;
    }
    return true;
}

} // namespace

auto DetectUiLanguageFromFingerprint(
        std::string_view defenseFormat) noexcept -> UiLanguage {
    if (defenseFormat == "防禦：%d") return UiLanguage::TraditionalChinese;
    if (defenseFormat == "Verteidigung: %d") return UiLanguage::German;
    if (defenseFormat == "Defensa: %d") return UiLanguage::Spanish;
    if (defenseFormat == "Défense : %d") return UiLanguage::French;
    if (defenseFormat == "Difesa: %d") return UiLanguage::Italian;
    if (defenseFormat == "방어력: %d") return UiLanguage::Korean;
    if (defenseFormat == "Obrona: %d") return UiLanguage::Polish;
    if (defenseFormat == "防御力: %d") return UiLanguage::Japanese;
    if (defenseFormat == "Defesa: %d") {
        return UiLanguage::BrazilianPortuguese;
    }
    if (defenseFormat == "Защита: %d") return UiLanguage::Russian;
    if (defenseFormat == "防御: %d") return UiLanguage::SimplifiedChinese;
    return UiLanguage::English;
}

auto RefreshUiLanguage(const D2RL::PluginContext* context) noexcept -> bool {
    if (context == nullptr) return false;
    const D2RL::LocalizationServiceV1* service{};
    if (context->QueryService(
            D2RL::ServiceId::Localization,
            D2RL::LocalizationServiceV1Version,
            &service) != D2RL::ServiceQueryResult::Success
        || !D2RL::HasLocalizationServiceV1Field(
            service,
            D2RL::LocalizationServiceV1RequiredSize)
        || service->getStringByKey == nullptr) {
        return false;
    }

    std::array<char, 128> buffer{};
    std::uint32_t required{};
    const auto result = service->getStringByKey(
        context,
        "ItemStats1h",
        buffer.data(),
        static_cast<std::uint32_t>(buffer.size()),
        &required);
    if (result != D2RL::Localization::Result::Success
        || required == 0U || required > buffer.size()
        || buffer[required - 1U] != '\0') {
        return false;
    }

    ActiveLanguage.store(
        DetectUiLanguageFromFingerprint(
            std::string_view(buffer.data(), required - 1U)),
        std::memory_order_release);
    return true;
}

void ResetUiLanguage() noexcept {
    ActiveLanguage.store(UiLanguage::English, std::memory_order_release);
}

auto CurrentUiLanguage() noexcept -> UiLanguage {
    return ActiveLanguage.load(std::memory_order_acquire);
}

auto UiLanguageCode(UiLanguage language) noexcept -> std::string_view {
    constexpr std::array Codes{
        std::string_view{"enUS"},
        std::string_view{"zhTW"},
        std::string_view{"deDE"},
        std::string_view{"esES/esMX"},
        std::string_view{"frFR"},
        std::string_view{"itIT"},
        std::string_view{"koKR"},
        std::string_view{"plPL"},
        std::string_view{"jaJP"},
        std::string_view{"ptBR"},
        std::string_view{"ruRU"},
        std::string_view{"zhCN"},
    };
    return Codes[LanguageIndex(language)];
}

auto UiText(UiTextId id, UiLanguage language) noexcept -> const char* {
    const auto textIndex = static_cast<std::size_t>(id);
    if (textIndex >= TextCatalog.size()) return "";
    return TextCatalog[textIndex][LanguageIndex(language)];
}

auto UiText(UiTextId id) noexcept -> const char* {
    return UiText(id, CurrentUiLanguage());
}

auto UiLocalizationCatalogIsComplete() noexcept -> bool {
    for (const auto& translation : TextCatalog) {
        for (const auto* text : translation) {
            if (text == nullptr || text[0] == '\0' || !Utf8Valid(text)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace RuffnecKk::MapSense
