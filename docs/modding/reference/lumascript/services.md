---
title: Services, events and LumaKit
shortTitle: Services and LumaKit
intro: Use services and LumaKit modules from LumaScript.
contentType: reference
---

## Using a service

Write `use svc.<name>` at the top of a module. The service is then called by the last part of its name, or by the name you give with `as`:

```lumascript
use svc.time
use svc.save.game            // called as game.
use svc.net.local as lan     // called as lan.
```

A service the game lacks, such as on an older game or a platform without it, makes the mod refuse to load and names the service.

## Optional services

`use optional svc.<name>` gives `none` when the game or the player did not give the service. Test it before you call it, with `if x != none`, `guard x != none else { return }`, or `if x == none { return }`. A call with no such test before it is a compile error.

## Permissions

Declare the permissions a service needs in `luma.json`. Using a service whose permission is not declared is a compile error that names the permission. For the list, see [Permissions](../../concepts/permissions.md).

## Events and waiting

A service result that waits on the outside world comes as an event at the start of a frame. You give a closure that receives it, or you `wait` for it in a task:

```lumascript
use svc.fs

task load_best() -> u64 {
    if let data = wait fs.read("best.bin") {
        return data.read_u64(0, big: false)
    }
    return 0
}
```

Handlers that a service runs for the whole mod, such as the frame and step handlers, are written with `on`, not called. `on frame` and `on step` are the handlers for `time`'s frame and step. For the rules, see [Lifecycle, options, frames and content](lifecycle.md) and [Tasks](tasks.md).

## Library mods

A library mod's service calls are its own, in its exports and in what they start. A request made inside an export is answered to the library.

## LumaKit

LumaKit modules are ordinary modules of the library mod `lumakit` (`use kit.player.form`). The language knows nothing special about them.

## Further reading

* [Calling services](../services/calling-services.md)
* [Services reference](../services/index.md)
* [LumaKit reference](../lumakit/index.md)
