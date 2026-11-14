# LumaKit: shop

Gives: buying things with coins or star bits.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub enum Currency { Coins, StarBits }
pub struct Ware { name: text, price: u32, gives: item.Gives, stock: u32? = none }
pub struct Shop { title: text, wares: list<Ware>, currency: Currency, keeper: ref<LiveActor>? = none }
pub fn open(s: Shop) -> pending<Ware?>      // none: left without buying
```

Data `kit/shop/<id>.json`. Built on: `kit.screen`, `kit.item`, `kit.talk`, `K.call` (`MR::incCoin`, `incStarPiece`
with negative counts, read from the game's counters), `save` (stock per file). Conformance: buy, too poor, out of
stock, stock kept across a reload. Corpus: 42. B: G. A: F.
