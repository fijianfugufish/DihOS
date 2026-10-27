/* Generated from the vendored NetSurf resources. */
static const char resource_default_css[]=R"DIHSCSS(/*
 * This file is part of NetSurf, http://netsurf-browser.org/
 */

/* Elements ordered as in the HTML 4.01 specification. */

html { display: block; }

head { display: none; }

body { display: block; margin: 8px; line-height: 1.33; }

div { display: block; }

h1 { display: block; font-size: 2em; font-weight: bold; margin: .67em 0; }
h2 { display: block; font-size: 1.5em; font-weight: bold; margin: .69em 0; }
h3 { display: block; font-size: 1.17em; font-weight: bold; margin: .83em 0; }
h4 { display: block; font-weight: bold; margin: 1.12em 0; }
h5 { display: block; font-size: .83em; font-weight: bold; margin: 1.5em 0; }
h6 { display: block; font-size: .75em; font-weight: bold; margin: 1.67em 0; }

address { display: block; font-style: italic; }

em { font-style: italic; }
strong { font-weight: bold; }
dfn { font-style: italic; text-decoration: underline; }
code { font-family: monospace; }
samp { font-family: monospace; }
kbd { font-family: monospace; font-weight: bold; }
var { font-style: italic; }
cite { font-style: italic; }
abbr { font-variant: small-caps; }
acronym { font-variant: small-caps; }

blockquote { display: block; margin: 1.12em 40px;}

q { font-style: italic; }

sub { vertical-align: sub; font-size: .83em; }
sup { vertical-align: super; font-size: .83em; }

p { display: block; margin: 1.12em 0; }

br[clear=left] { clear: left; }
br[clear=right] { clear: right; }
br[clear=all] { clear: both; }

pre { display: block; font-family: monospace; white-space: pre; margin-bottom: 1em; }

ins { color: green; text-decoration: underline; }
del { color: red; text-decoration: line-through; }

ul { display: block; padding-left: 40px; margin: 1.12em 0;
	list-style-type: disc; }
ol { display: block; padding-left: 40px; margin: 1.12em 0;
	list-style-type: decimal; }
li { display: list-item; }

ul ul { list-style-type: circle; }
ul ul ul { list-style-type: square; }

ol ul, ul ol, ul ul, ol ol { margin-top: 0; margin-bottom: 0; }

dl { display: block; padding-left: 1.5em; margin: 1em; }
dt { display: block; font-weight: bold; }
dd { display: block; padding-left: 1em; margin-bottom: 0.3em; }

dir { display: block; padding-left: 1.5em; margin: 1.12em 0; }
menu { display: block; padding-left: 1.5em; margin: 1.12em 0; }

table { display: table; border-spacing: 2px; }

table[border], table[border] td, table[border] tr { border-color: grey; }

caption { display: table-caption; }

thead { display: table-header-group; vertical-align: middle; }
tfoot { display: table-footer-group; vertical-align: middle; }
tbody { display: table-row-group; vertical-align: middle; }

colgroup { display: table-column-group; }
col { display: table-column; }

table > tr { vertical-align: middle; }

tr { display: table-row; vertical-align: inherit; }
td, th { display: table-cell; vertical-align: inherit; padding: 1px; }
th { font-weight: bold; text-align: center; }

a:link { color: #00f; text-decoration: underline; }
a:visited { color: #609; }

img { color: #888; }

center { display: block; }

tt { font-family: monospace; }
i { font-style: italic; }
b { font-weight: bold; }
big { font-size: 1.17em; }
small { font-size: .83em; }
strike, s { text-decoration: line-through; }
u { text-decoration: underline; }

hr { display: block; margin: 0.5em auto; border: 1px inset #888; }
hr[noshade] { border-style: solid; }

noframes { display: none; }

iframe { width: 19em; height: 10em; }

form { display: block; }

input, button { background-color: #fff; color: #000; text-align: left;
	font-family: sans-serif; width: auto; height: auto; overflow: hidden;
	border: 1px solid #444; padding: 2px 3px; line-height: 1.33;
	margin: 1px; box-sizing: border-box; }
input[disabled] { background-color: #ddd; color: #333; }
input[type=button], input[type=reset], input[type=submit], button {
	background-color: #d9d9d9; color: #000; text-align: center;
	border: 2px outset #d9d9d9; padding: 1px 0.5em; }
input[type=image] { background-color: transparent; color: #000;
	border: none; padding: 0 2px; box-sizing: content-box; }
input[type=checkbox], input[type=radio] { background-color: transparent;
	border: none; padding: 0 0.1em; }
input[type=file] { background-color: #d9d9d9; color: #000; font-style: italic;
	border: 2px inset #d9d9d9; padding: 1px 2px; }
input[align=left] { float: left; }
input[align=right] { float: right; }

select { background-color: #d9d9d9; color: #000; text-align: left;
	font-family: sans-serif; width: auto; height: auto; overflow: hidden;
	border: medium inset #d9d9d9; padding: 1px 3px 1px 2px;
	white-space: nowrap; margin: 1px; }
select:after { content: "\25bc"; border-left: 2px ridge #d9d9d9; }

textarea { background-color: #fff; color: #000; text-align: left;
	font-family: monospace; width: auto; height: auto; overflow: scroll;
	margin: 1px; border: 1px solid #333; padding: 1px 3px; }
textarea[disabled] { background-color: #ddd; color: #333; }

fieldset { display: block; border: thin solid #888; margin: 1.12em 0; }

noembed, script, style, title { display: none; }

/* HTML5 elements */
article, aside, figcaption, figure, footer, header, main, nav, section {
	display: block;
}

mark {
	background: yellow;
	color: black;
}
)DIHSCSS";
static const char resource_quirks_css[]=R"DIHSCSS(/* Quirks mode stylesheet for NetSurf */

/**
 * Netscape/IE 4 compatibility.
 *
 * See https://developer.mozilla.org/en/Fixing_Table_Inheritance_in_Quirks_Mode
 */
table {
	/* Reset font properties (except family) */
	font-size: medium;
	font-style: normal;
	font-variant: normal;
	font-weight: normal;
}
)DIHSCSS";
static const char resource_internal_css[]=R"DIHSCSS(@import url("about:nscolours.css");

html, body {
	margin: 0;
	padding: 0; }

html {
	padding: 2em;
}

body {
	font-family: sans-serif;
	font-size: 94%;
	margin: 0 auto;
	position: relative;
	border-width: 2px;
	border-style: solid; }

h1, h2, h3, h4, h5, h6, h7, p, ul, dl, ol, table {
	margin-left: 5mm;
	margin-right: 5mm; }

h1 {
	font-size: 200%;
	font-weight: bold;
	padding: 5mm;
	margin: 0;
	border-bottom-width: 2px;
	border-bottom-style: solid; }

h2 {
	font-size: 160%;
	padding-top: 5mm;
	border-top-width: 1px;
	border-top-style: solid; }

h1+h2 {
	border-top: none;
}

h3 {
	font-size: 140%;
	font-weight: bold; }

h4 {
	font-size: 120%;
	font-weight: bold; }

h5 {
	font-size: 120%;
	font-style: italic; }

h1, h2, h3 {
	clear: both; }

dl {
	padding: 0; }

dt {
	padding: 0;
	font-weight: bold;
	margin-bottom: 0.3em; }

dd {
	padding: 0;
	margin-left: 5mm;
	margin-bottom: 1em; }

dd > ul {
	margin-top: 0;
	margin-left: 0.5em;
	padding-left: 0;
	margin-bottom: 1em; }

.footer {
	clear: both;
	font-style: italic;
	text-align: right; }

.footer p {
	margin-top: 1.5em;
	padding-top: 0.4em;
	border-top-width: 2px;
	border-top-style: solid; }

input.default-action {
	color: white;
	background: rgb(0, 96, 223);
	border-color: rgb(0, 96, 223);
}

/*
 * Directory Listing Style
 */

body#dirlist {
	padding-bottom: 2em; }

body#dirlist p {
	padding: 2px 5mm;
	margin: 0; }

body#dirlist div {
	display: table;
	width: 94%;
	margin: 5mm auto 2em auto;
	padding: 0; }

body#dirlist strong {
	display: table-row;
	margin: 0;
	padding: 0;
	font-size: 120%;
}

body#dirlist a {
	display: table-row;
	margin: 0;
	padding: 0; }

body#dirlist span {
	display: table-cell;
	vertical-align: middle; }

body#dirlist a > span {
	padding-top: 0.1em; }

body#dirlist em > span {
	padding-bottom: 1px; }

body#dirlist a + a>span {
	border-top-width: 1px;
	border-top-style: solid; }

body#dirlist a {
	background-position: 4px 50%;
	background-repeat: no-repeat;
}

body#dirlist a.dir {
	background-image: url("resource:icons/directory.png"); }

body#dirlist a.file {
	background-image: url("resource:icons/content.png"); }

body#dirlist span.name {
	padding-left: 27px;
	min-height: 20px;}

body#dirlist a.dir > span.name {
	font-weight: bold; }

body#dirlist a.dir > span.type {
	font-weight: bold; }

body#dirlist span.size {
	text-align: right;
	padding-right: 0.3em; }

body#dirlist span.size + span.size {
	text-align: left;
	padding-right: 0; }


/*
 * certificate display style
 */
body#certificate table.info {
	width: 90%;
	margin: 1.2em auto 0;
}

body#certificate table.info th {
	width: 14em;
	vertical-align: top;
	text-align: right;
	font-weight: bold;
	font-family: sans-serif;
	padding-right: 1em;
}

body#certificate table.info td.data {
	font-family: monospace;
	font-size: 90%;
}


/* 
 * configuration listing style 
 */

body#configlist table.config {
	border-spacing: 0;
	font-size: 85%;
	margin: 1.3em 0;
	width: 100%;
	box-sizing: border-box }

body#configlist table.config tr:first-child th {
	text-align: left;
	font-weight: bold;
	font-family: sans-serif; }

body#configlist table.config tr th {
	text-align: left;
	font-weight: normal;
	font-family: monospace; }

body#configlist table.config tr+tr th,
body#configlist table.config tr+tr td {
	border-top-width: 1px;
	border-top-style: solid;
}

body#configlist table.config td, body#configlist table.config th {
	padding-left: 1em; }

body#configlist table.config tr *:nth-child(3) {
	padding-left: 3em; }

body#configlist table.config tr *:nth-child(4) {
	padding-left: 3em;
	font-family: monospace;
	padding-right: 1em; }

body#configlist .null-content {
	font-style: italic; }


/*
 * about:imagecache
 */

p.imagecachelist {
	border-spacing: 0px;
	margin-top: 1.2em;
	margin-bottom: 1.2em;
	display: table;
}

p.imagecachelist strong, p.imagecachelist a {
	display: table-row;
}

p.imagecachelist span {
	padding: 2px 0.5em;
	display: table-cell;
}

p.imagecachelist a+a span {
	border-top-width: 1px;
	border-top-style: solid;
}

/*
 * authentication query styling
 */

body#authentication {
    max-width: 45em;
}

body#authentication form {
  /* Just to center the form on the page */
  margin: 0 auto;
  /* To see the outline of the form */
  padding: 1em;
}

body#authentication table {
  box-sizing: border-box;
  width: 100%;
  margin: 1.5em auto;
}

body#authentication label {
  /* To make sure that all labels have the same size and are properly aligned */
  display: inline-block;
  text-align: right;
}

body#authentication table th {
  text-align: right;
}

body#authentication table td {
  text-align: left;
}

body#authentication table input {
  /* To give the same size to all text fields */
  width: 20em;
  box-sizing: border-box;
  margin-right: 10%;

  /* To harmonize the look & feel of text field border */
  border: 1px solid #999;
}

body#authentication div#buttons {
  text-align: right;
  margin-right: 1em;
}

body#authentication div#buttons input#login {
  margin-right: 1em;
}

/*
 * privacy query styling
 */

body#privacy {
  max-width: 45em;
}

body#privacy form {
  /* Just to center the form on the page */
  margin: 0 auto;
  /* To see the outline of the form */
  padding: 1em;
}

body#privacy form div + div {
  margin-top: 1em;
}

body#privacy div#buttons {
    text-align: right;
    margin-right: 1em;
}

body#privacy div#buttons input#back {
  margin-right: 1em;
}

/*
 * timeout query styling
 */

body#timeout {
  max-width: 45em;
}

body#timeout form {
  /* Just to center the form on the page */
  margin: 0 auto;
  /* To see the outline of the form */
  padding: 1em;
}

body#timeout form div + div {
  margin-top: 1em;
}

body#timeout div#buttons {
    text-align: right;
    margin-right: 1em;
}

body#timeout div#buttons input#back {
  margin-right: 1em;
}

/*
 * fetch error query styling
 */

body#fetcherror {
  max-width: 45em;
}

body#fetcherror form {
  /* Just to center the form on the page */
  margin: 0 auto;
  /* To see the outline of the form */
  padding: 1em;
}

body#fetcherror form div + div {
  margin-top: 1em;
}

body#fetcherror div#buttons {
    text-align: right;
    margin-right: 1em;
}

body#fetcherror div#buttons input#back {
  margin-right: 1em;
}
)DIHSCSS";
static const char resource_adblock_css[]=R"DIHSCSS(/* From http://texturizer.net/firefox/adblock.html */

/* Block Ads v.2004-06-12 by Neil Jenkins
   Portions of this ruleset are derived from the ad blocking css by Joe Francis at:
   http://www.floppymoose.com/
   Some code also contributed by Mike Hokenson:
   http://www.gozer.org/mozilla/ad_blocking
*/ 

iframe[src*="/ad-"],
iframe[src*="/ad."],
iframe[src*="/ad/"],
iframe[src*="/ad_"],
iframe[src*="/adframe"],
iframe[src*="/ads"],
iframe[src*="/adv"],
iframe[src*="/adx"],
iframe[src*=".ad"],
iframe[src*="_Banner"],
iframe[src*="BET365"],
iframe[src*="/click"],
iframe[src*="?click"],
iframe[src*="googlesyndication"],
iframe[src*="promos"],
iframe[name="bsadframe"],
iframe[id="adFrame"],
iframe[id="splashFrame"],
img[src*="/ad-"],
img[src*="/ad."],
img[src*="/ad/"],
img[src*="/ad_"],
img[src*="/ads"],
img[src*="/adv"],
img[src*="/adx"],
img[src*=".ad"],
img[src*="_ad_"],
img[src*="/click"],
img[src*="?click"],
img[src*="Editorial_Promos"],
img[src*="/toms_skyscraper_"],
img[src$="images/logos/amazon_international.gif"],
img[src*="freshmeat.net/button.gif"],
img[src*="pic.geocities.com/images/"],
img[src*="i.imdb.com/Vpics/"],
img[src*="i.imdb.com/Photos/CMSIcons"],
img[src*="i.imdb.com/Icons/apix/"],
img[src*="i.imdb.com/Icons/kpix/"],
img[src*="i.imdb.com/apix/"],
img[src*="sportsbybrooks.com/farkbutton.gif"],
img[alt*="advert"],
img[alt*="Click Here To Visit Our Sponsor"],
img[alt*="Sponsored Links"],
img[name="fixedSpImg"],
a:link[href*="/ad-"] img,
a:link[href*="/ad."] img,
a:link[href*="/ad/"] img,
a:link[href*="/ad_"] img,
a:link[href*="/ads"] img,
a:link[href*="/adv"] img,
a:link[href*="/adx"] img,
a:link[href*=".ad"] img,
a:link[href*="/banner"] img,
a:link[href*="/click.cgi"] img,
a:link[href*="/clickcgi?"] img,
a:link[href*="/clickover"] img,
a:link[href*="/clickthr"] img,
a:link[href*="/click-"] img,
a:link[href*="=click"] img,
a:link[href*="?click"] img,
a:link[href*="/A="] img,
table table table[width="346"][height="280"],
table[class="metatable"],
table[id="RefAd"],
table[cellspacing="0"][width="160"][height="329"][bgcolor="#000000"],
table[cellpadding="5"][width="150"][style="border: 1px dashed rgb(221, 170, 170); background-color: rgb(236, 248, 255);"],
table[border="0"][height="600"][width="120"],
table[height="90"][cellspacing="0"][cellpadding="0"][width="728"][border="0"],
table[border="0"][cellpadding="3"][cellspacing="0"][height="40"][width="100%"],
table[border="0"][cellpadding="0"][cellspacing="4"][align="right"][width="20%"],
table[border="2"] td[valign="top"][bgcolor="#ffffff"] table[width="138"],
table[border="0"][cellpadding="1"][cellspacing="0"][width="626"][bgcolor="#6699cc"],
table[bordercolor="red"][height="225"][cellspacing="0"][cellpadding="0"][width="190"][bgcolor="white"][border="0"],
table[width="336"][border="0"][cellspacing="0"][cellpadding="0"][align="right"][style="margin-left: 10px; margin-bottom: 10px;"],
table[width="770"][border="0"] tr[valign="top"]
td[width="160"],
td[class="cnnNavAd"],
td[class="RHBarBody"],
td[class="sl-table"],
td[style="border-bottom: 1px solid rgb(204, 204, 204); padding: 7px 3px; background-color: rgb(255, 254, 249);"],
td span[class="aoltextad"],
form[name="frmLargeForm"],
form[action*="click_lx.cgi"],
form[action^="http://rd.yahoo.com/"],
form[action=""][name$="SoapCity"],
form[action=""][name^="728x90"],
form[action=""][name^="300x250"],
form[method="get"][action*="martindale"][name="MDH"],
embed[type="application/x-shockwave-flash"][src*="_Banner"],
embed[type="application/x-shockwave-flash"][src*="/ad"],
embed[type="application/x-shockwave-flash"][href*="/ad"],
embed[type="application/x-shockwave-flash"][src*=".ad"],
embed[type="application/x-shockwave-flash"][href*=".ad"],
embed[type="application/x-shockwave-flash"][src*="?click"],
embed[type="application/x-shockwave-flash"][href*="?click"],
embed[type="application/x-shockwave-flash"][src*="/click"],
embed[type="application/x-shockwave-flash"][href*="/click"],
embed[type="application/x-shockwave-flash"][href*="promos"],
embed[type="application/x-shockwave-flash"][href*="=click"],
embed[type="application/x-shockwave-flash"][href*="redirect"],
embed[type="application/x-shockwave-flash"][href*="/A="],
embed[type="application/x-shockwave-flash"][name*="advert"],
embed[type="application/x-shockwave-flash"][name*="banner"],
div[id="adstrip"],
div[id*="advert"],
div[id="dropin"],
div[id="dwindow"],
div[id="dcv_EchangeLay"],
div[id="mntl"],
div[id*="popup"],
div[id="quack"],
div[id="splashLayer"],
div[id*="sponsor"],
div[id="yschsec"],
div[class="ovt"],
div#contextualLinks,
div[style="border: 1px solid rgb(221, 221, 221); padding: 5px; background-color: rgb(255, 255, 255);"],
object[id="widgeth"],
*[class="ad"],
*[class*="eSext"],
*[name*="klipad"],
*[style*="/ads/"],
*[id*="popover_"],
*[src*="120x80"],
*[src*="?ad"],
*[src*="-ad-"],
*[src*="/adimage"],
*[src*="/adimg"],
*[src*="advert"],
*[src*="=click"],
*[src*="banners."],
*[src*="banner_ads"],
*[src*="/images/dinkus/more-arrow-lo.gif"],
*[src*="/javaImages/ec/"],
*[src*="/offer"],
*[src*="/puffboxes/"],
*[src*="/sponsors/"],
*[src*="_sponsor_"],
*[src*="/us.yimg.com/a/ya"],
*[src*=".adbutler.de"],
*[src*=".nedstatbasic.net"],
*[src*=".tradedoubler.com"],
*[src*=".travelocity./Sponsor_gifs/"],
*[src*="a.consumer.net"],
*[src*="bannermania.nom.pl"],
*[src*="cash4banner.com"],
*[src*="crazypopups.com"],
*[src*="hit-now.com"],
*[src*="hitbox.com"],
*[src*="img.thebugs.ws"],
*[src*="kinghost.com/ban/"],
*[src*="klipmart"],
*[src*="popupad.net/ats/"],
*[src*="public"][src*="ad"],
*[src*="/publicidad/"],
*[src*="qkimg.net"],
*[src*="qksrv.net"],
*[src*="rcm-images.amazon.com"],
*[src*="rcm.amazon.com"],
*[src*="salon.com/Creatives"],
*[src*="sexcounter."],
*[src*="sideads/"],
*[src*="statse.webtrendslive.com"],
*[href*="logging.to"],
*[href*="/referral/"],
*[href*="tryaol"],
*[href*="websponsors"],
*[src*="banman.asp"],
*[href*="banman.asp"],
*[src*="bannerman"],
*[href*="bannerman"],
*[src*="?banner"],
*[href*="?banner"],
*[src*="/banner/"],
*[href*="/banner/"],
*[src*="/jump/"],
*[href*="/jump/"],
*[src*="adfarm"],
*[href*="adfarm"],
*[src*="advertising.com"],
*[href*="advertising.com"],
*[src*="ar.atwola.com"],
*[href*="ar.atwola.com"],
*[src*="atdmt.com"],
*[href*="atdmt.com"],
*[src*="bluestreak.com"],
*[href*="bluestreak.com"],
*[src*="clickxchange.com"],
*[href*="clickxchange.com"],
*[src*="doubleclick"],
*[href*="doubleclick"],
*[src*="falkag.net"],
*[href*="falkag.net"],
*[src*="fastclick.com"],
*[href*="fastclick.com"],
*[src*="mediaplex"],
*[href*="mediaplex"],
*[src*="netdirect.nl"],
*[href*="netdirect.nl"],
*[src*="spinbox."],
*[href*="spinbox."],
*[src*="tradedoubler.com"],
*[href*="tradedoubler.com"],
*[src*="transfer.go"],
*[href*="transfer.go"],
*[src*=".tribalfusion"],
*[href*=".tribalfusion"],
*[src*="valueclick"],
*[href*="valueclick"],
*[src*="http://216.92.211.171/"],
*[href*="http://216.92.211.171/"],
*[src*="http://216.92.21.16/"],
*[href*="http://216.92.21.16/"] {
        display: none /*!important*/;
}

img[src*=".advance"],
img[src*="/click_"],
img[src*="/cnn/images/clickability/"],
img[src*="/logo2.m"],
img[src$="/banners/com000.jpg"],
a:link[href*=".add"] img,
a:link[href*="admin"] img,
a:link[href*="adobe"] img,
a:link[href*=".adp"] img,
a:link[href*="/advance"] img,
a:link[href*="click.mp3"] img,
a:link[href*="/imgres?imgurl="] img,
a:link[href*=":head"] img,
a:link[href*="http://nettracker."] img,
embed[type="application/x-shockwave-flash"][src*="gallery"] {
        display: inline /*!important*/;
}
)DIHSCSS";
