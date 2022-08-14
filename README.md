[![Donate](https://img.shields.io/badge/Donate-PayPal-green.svg)](https://www.paypal.com/donate/?business=MP6AZ5W3Z89MU)

# botterage

This is oantby's Twitch Chat bot, botterage.  It was designed for twitch.tv/slayerage,
but is built around the idea of being reusable for other channels.

## Usage

If you aren't overly technical, you might want a hand getting this bot set up.
I tried to make it relatively easy, and it will get more so over time, but certain
things still require direct database access to set up. If you're pretty technical,
then see "Running This Bot" below.

**If you've reached this sentence, read the entire rest of the usage section before
proceeding, or you will find out sometime in the future that you missed a huge
chunk of functionality.**

Once initial set up is done, you'll be looking at a few out-of-the-box commands
you can run:

### Adding commands

```
!addcmd [-cd=x] [-ul=y] command-pattern this is the command response
```

Adds a new command that runs when `command-pattern` is matched by a message.
`command-pattern` is treated as a [regular expression](https://regexr.com/).
It must match all the way to the beginning and end of a word, but need not
match the whole message.

`-cd=x` is optional, specifying a cooldown `x` in seconds for the command.

`-ul=y` is optional, specifying a minimum user level (`any`, `sub`, `mod`, or `owner`)
to run this command.

If both cooldown and user level are specified, they must be done in the order
shown above.

### Editing Commands

```
!editcmd [-cd=x] [-ul=y] command-pattern new command response
```

Edits the command `command-pattern`, updating the response and (optionally)
the user level and/or cooldown. See the notes in `!addcmd` for ordering and
usage of cooldown and user level.

Instead of using the full `command-pattern` to identify the command, you can
use a single word that would've matched the command *if* that word doesn't match
any other commands.

### Deleting Commands

```
!delcmd command-pattern
```
Deletes the command `command-pattern`. Instead of using the full pattern
to identify the command, you can use a single word that would've matched the command
*if* that word doesn't match any other commands.

### Declaring and Using variables

```
!declare var-name variable value
!declare var-name number
```

Declares a variable that can then be placed into the responses of commands.
This variable can be a string or a number. Iff the variable is a number, then
commands can also increment or decrement the value of the variable by one as follows:

```
!declare deathcount 0
--> I've set $(deathcount) to 0
!addcmd -cd=0 -ul=mod ^!death\+ The death counter has increased to ++$(deathcount)
--> Command ^!death\+ added successfully
!addcmd -cd=0 -ul=mod ^!death- The death counter has decreased to --$(deathcount)
--> Command ^!death- added successfully
!addcmd ^!deathcount The death count is at $(deathcount)
--> Command ^!deathcount added successfully
!death+
--> The death counter has increased to 1
!deathcount
--> The death count is at 1
!death-
--> The death counter has decreased to 0
```

String variables cannot be changed except with a new `!declare` command,
but can be used to keep multiple commands consistent:

```
!declare favfruit strawberries
--> I've set $(favfruit) to strawberries
!addcmd ^!fruit The streamer's favorite fruits are $(favfruit)
--> Command ^!fruit added successfully
!addcmd stream.*like.*fruit Yes, the streamer likes fruit. Particularly, $(favfruit)
--> Command stream.*like.*fruit added successfully
Does the streamer like fruit?
--> Yes, the streamer likes fruit. Particularly, strawberries
```

### Deleting Variables

```
!delvar var-name
```

Deletes the variable `var-name`.

### Static Variables

There are some variables, not declared with `!declare`, that are supported by all
normal commands. These can be used to fill in information in the bot's response.
The functionality of these variables is provided by the `commands` module.
Some variables are provided by other modules, as listed, but `commands` must
be enabled for any of them to work.
The built-ins are:

- `$(user)` - replaced with the name of the user invoking the command.
- `$(toUser)` - replaced with the first word starting with `@` in the invoking message,
or the invoking user if no `@` was specified.
- `$(greeting)` - replaces the whole message with the greeting for the user.
This is usually "Hey there $(user) casHey //", but can be customized per-user
in the `users` database table.
- `$(addCom)` - treats the command as if it is `!addcmd`, allowing you to
rename `!addcmd` to whatever you want
- `$(editCom)` - treats the command as if it is `!editcmd`, allowing you to
rename `!editcmd` to whatever you want
- `$(delCom)` - treats the command as if it is `!delcmd`, allowing you to
rename `!delcmd` to whatever you want
- `$(random)` - always used as `($(random) low high)`, replaced with a random
number in the range `[low, high]`.
- `$(declare)` - treats the command as if it is `!declare`, allowing you to
rename `!declare` to whatever you want.
- `$(delVar)` - treats the command as if it is `!delvar`, allowing you to
rename `!delvar` to whatever you want.

Provided by the `uptime` module:

- `$(uptime)` - replaced with how long the stream has been active in hours,
minutes, and seconds. See "Settings" under "Customization" for changing the
response when the stream is offline.
- `$(puptime)` - replaced with how long the stream has been active in DOGGO
time (time times seven).

Provided by the `twitchevents` module:

- `$(shoutout)` - Replaces the response with a shoutout for a user. The message, as well
as additional followup messages, are defined on the settings page.
- `$(raid)` - Replaces the response with a note to everyone that we're raiding
a channel! The message, as well as followup messages, are defined on the
settings page.

## Running This Bot

This repository ships with a docker-compose file (that, *gasp* has a default password
in it). Docker-compose is a lovely, easy way to get this bot up and running,
by simply running `docker-compose up -d` from within the top directory.
Obviously, you're welcome to change the password in there. The more appropriate
thing for me to do would be to put it in a secrets engine and just reference
that, but the password is strictly for things that are only locally accessible,
and, as noted below, I'm not trying to secure an enterprise here.

### But Logan, there are way better options than `docker-compose`!

In a sense, you're right! `docker-compose` isn't the most powerful of orchestrators,
and it certainly can act a little counterintuitive compared to a docker swarm,
etc. But, the fact that you're able to point that out tells me that you know
enough to also easily convert that compose file to a configuration for the
orchestrator of your choosing. As for why I continue to use it: it's easy.
I'm not running some enterprise server or something, and this is an easy way
to spin up three images with backing storage as needed that just works.

### Customization

There are a couple key methods to changing how botterage acts:

1. Enabling/disabling modules
2. Settings

Modules are individual chunks of functionality for botterage - maybe
you want it to respond to commands, but not to break incoming pyramids.
You can do that, by disabling the pyramids module in the database. That, at
this time, must be done directly in the database (no interface for it).

Settings are values used by modules or the bot as a whole to change how they act.
Settings can be changed directly in the `variables` table, or can be managed
via web page. For this, an FCGI is exposed at port 8000. Point your favorite
web browser at it and visit `http://your-host/settings`, and you'll be off
to the races. As this page knows the necessary variables to get the bot started,
it's also a good first place to go. Once the required variables are set, it
secures itself by requiring login through twitch. To be considered a valid user,
you must either be one of those listed in `bot_admins` or the streamer listed
in `twitch_channel`.

### Commands List

If you find yourself wanting a commands list beyond just looking at the database,
you're in luck! See the description of Settings above; the web server also
exposes `http://your-host/commands_list`. I won't act like it's the coolest
commands list in the world, but it looks half-decent and is quite performant.

# Technical Notes

Here's some things that, to me, feel worth asking about.

* Why do you open the executable as a dynamic library to read the `_init` functions?

I really wanted the ability to enable and disable modules without having to
recompile the program. To do this, I initially considered compiling all the modules
as dynamic libraries, and loading them based on the database entries. However,
I ran into uncertainties in terms of guarantees of C++ compilers keeping padding
etc. consistent. Now, I could specify my alignment and get rid of that type of
concern, but it wasn't something I was ready to play with yet. So, instead, I
compile the whole thing as one binary, then look up which modules to load, and
load them from the main binary.

Side note: as you can tell if you're reading the code, `invoke_hook` is by no
means specific to the `_init` functions; they're just the only thing use it for
right now. Prior to the "module" approach to developing this bot, I had devised
the "stack of listeners for each actionable event" approach encompassed by,
e.g. `commands.cpp` adding a function to `chatMsgListeners`. To be more complete
in the modules approach, I would have something more like
`invoke_hook("chat_message", (void *)&messageInfo)` or something like that.
I didn't love the cast to void pointer in there, and the approach I had felt
cleaner. If I keep this project going, I might explore some better approaches.

* Why is there so much excess functionality in `twitchapi`?

This bot used to do a lot more with twitch-type stuff - the primary thing
being tracking of "points" for people in chat. However, with twitch adding
that functionality natively, it became redundant for the bot to handle it.
That said, the module stuck around because it's used to collect uptime
and to 

* Why is the commit log so short?

I orphaned the heck out of this repo because it used to include 1) some of my
personal information and 2) secret variables (api keys, etc) in header files.
I've moved those to the database and environment, so this thing is ready to
be public.

* Isn't the twitch chat channel (IRC) unencrypted?

~~Yes. It is. That certainly is a downside, isn't it? Twitch also has a websocket
approach to chat, which someday I may use. But, as it stands, a websocket client
in c++ would take me more than a handful of minutes, and I personally like
building that kind of thing out myself - it's a nice way to keep the brain
flexible. Plus, I've already written a websocket server before, so the client
should be easy, relatively speaking - just need to not be lazy.~~

Nope.

## Support Me

I'm not going to act like I'm a huge help to the open source community or something.
I'm not overly active, and I "contribute" by making things I like and posting
them. There's nothing noble about it. BUT, if you like what I make and you
want me to spend more time making that kind of thing, or you just want to
give me a pat on the back, you are more than welcome to through paypal:

[![Donate](https://img.shields.io/badge/Donate-PayPal-green.svg)](https://www.paypal.com/donate/?business=MP6AZ5W3Z89MU)