/**
 * @file key.c
 * @author Joe Wingbermuehle
 * @date 2004-2006
 *
 * @brief Key binding functions.
 *
 */

#include "jwm.h"
#include "key.h"

#include "client.h"
#include "clientlist.h"
#include "command.h"
#include "error.h"
#include "misc.h"
#include "root.h"
#include "tray.h"

#define MASK_NONE    0
#define MASK_SHIFT   (1 << ShiftMapIndex)
#define MASK_LOCK    (1 << LockMapIndex)
#define MASK_CTRL    (1 << ControlMapIndex)
#define MASK_MOD1    (1 << Mod1MapIndex)
#define MASK_MOD2    (1 << Mod2MapIndex)
#define MASK_MOD3    (1 << Mod3MapIndex)
#define MASK_MOD4    (1 << Mod4MapIndex)
#define MASK_MOD5    (1 << Mod5MapIndex)

typedef struct ModifierNode {
   char           name;
   unsigned int   mask;
} ModifierNode;

static ModifierNode modifiers[] = {

   { 'C',   MASK_CTRL      },
   { 'S',   MASK_SHIFT     },
   { 'A',   MASK_MOD1      },
   { '1',   MASK_MOD1      },
   { '2',   MASK_MOD2      },
   { '3',   MASK_MOD3      },
   { '4',   MASK_MOD4      },
   { '5',   MASK_MOD5      },
   { 0,     MASK_NONE      }

};

typedef struct KeyNode {

   /* These are filled in when the configuration file is parsed */
   int key;
   MouseContextType context;
   unsigned int state;
   KeySym symbol;
   char *command;
   struct KeyNode *next;

   /* This is filled in by StartupKeys if it isn't already set. */
   unsigned code;

} KeyNode;

typedef struct LockNode {
   KeySym symbol;
   unsigned int mask;
} LockNode;

static LockNode lockMods[] = {
   { XK_Caps_Lock,   0 },
   { XK_Num_Lock,    0 }
};

static KeyNode *bindings;
unsigned int lockMask;

static unsigned int GetModifierMask(XModifierKeymap *modmap, KeySym key);
static KeySym ParseKeyString(const char *str);
static char ShouldGrab(ActionType key);
static void GrabKey(KeyNode *np, Window win);

/** Initialize key data. */
void InitializeKeys(void)
{
   bindings = NULL;
   lockMask = 0;
}

/** Startup key bindings. */
void StartupKeys(void)
{

   XModifierKeymap *modmap;
   KeyNode *np;
   TrayType *tp;
   int x;

   /* Get the keys that we don't care about (num lock, etc). */
   modmap = JXGetModifierMapping(display);
   for(x = 0; x < sizeof(lockMods) / sizeof(lockMods[0]); x++) {
      lockMods[x].mask = GetModifierMask(modmap, lockMods[x].symbol);
      lockMask |= lockMods[x].mask;
   }
   JXFreeModifiermap(modmap);

   /* Look up and grab the keys. */
   for(np = bindings; np; np = np->next) {

      /* Determine the key code. */
      if(!np->code) {
         np->code = JXKeysymToKeycode(display, np->symbol);
      }

      /* Grab the key if needed. */
      if(ShouldGrab(np->key)) {

         /* Grab on the root. */
         GrabKey(np, rootWindow);

         /* Grab on the trays. */
         for(tp = GetTrays(); tp; tp = tp->next) {
            GrabKey(np, tp->window);
         }

      }

   }
}

/** Shutdown key bindings. */
void ShutdownKeys(void)
{
   ClientNode *np;
   TrayType *tp;
   unsigned int layer;

   /* Ungrab keys on client windows. */
   for(layer = 0; layer < LAYER_COUNT; layer++) {
      for(np = nodes[layer]; np; np = np->next) {
         JXUngrabKey(display, AnyKey, AnyModifier, np->window);
      }
   }

   /* Ungrab keys on trays, only really needed if we are restarting. */
   for(tp = GetTrays(); tp; tp = tp->next) {
      JXUngrabKey(display, AnyKey, AnyModifier, tp->window);
   }

   /* Ungrab keys on the root. */
   JXUngrabKey(display, AnyKey, AnyModifier, rootWindow);
}

/** Destroy key data. */
void DestroyKeys(void)
{
   while(bindings) {
      KeyNode *np = bindings->next;
      if(bindings->command) {
         Release(bindings->command);
      }
      Release(bindings);
      bindings = np;
   }
}

/** Grab a key. */
void GrabKey(KeyNode *np, Window win)
{
   unsigned int x;
   unsigned int index, maxIndex;
   unsigned int mask;

   /* Don't attempt to grab if there is nothing to grab. */
   if(!np->code) {
      return;
   }

   /* Grab for each lock modifier. */
   maxIndex = 1 << (sizeof(lockMods) / sizeof(lockMods[0]));
   for(index = 0; index < maxIndex; index++) {

      /* Compute the modifier mask. */
      mask = 0;
      for(x = 0; x < sizeof(lockMods) / sizeof(lockMods[0]); x++) {
         if(index & (1 << x)) {
            mask |= lockMods[x].mask;
         }
      }
      mask |= np->state;

      /* Grab the key. */
      JXGrabKey(display, np->code, mask, win,
         True, GrabModeAsync, GrabModeAsync);

   }

}

/** Get the key action from an event. */
ActionType GetKey(MouseContextType context, unsigned state, unsigned code)
{
   KeyNode *np;

   /* Remove modifiers we don't care about from the state. */
   state &= ~lockMask;

   /* Loop looking for a matching key binding. */
   for(np = bindings; np; np = np->next) {
      if(np->context == context && np->state == state && np->code == code) {
         return np->key;
      }
   }

   return ACTION_NONE;
}

/** Run a command invoked from a key binding. */
void RunKeyCommand(MouseContextType context, unsigned state, unsigned code)
{
   KeyNode *np;

   /* Remove the lock key modifiers. */
   state &= ~lockMask;

   for(np = bindings; np; np = np->next) {
      if(np->context == context && np->state == state && np->code == code) {
         RunCommand(np->command);
         return;
      }
   }
}

/** Show a root menu caused by a key binding. */
void ShowKeyMenu(MouseContextType context, unsigned state, unsigned code)
{
   KeyNode *np;

   /* Remove the lock key modifiers. */
   state &= ~lockMask;

   for(np = bindings; np; np = np->next) {
      if(np->context == context && np->state == state && np->code == code) {
         const int button = GetRootMenuIndexFromString(np->command);
         if(JLIKELY(button >= 0)) {
            ShowRootMenu(button, -1, -1, 1);
         }
         return;
      }
   }
}

/** Determine if a key should be grabbed on client windows. */
char ShouldGrab(ActionType key)
{
   switch(key & 0xFF) {
   case ACTION_NEXT:
   case ACTION_NEXTSTACK:
   case ACTION_PREV:
   case ACTION_PREVSTACK:
   case ACTION_CLOSE:
   case ACTION_MIN:
   case ACTION_MAX:
   case ACTION_SHADE:
   case ACTION_STICK:
   case ACTION_MOVE:
   case ACTION_RESIZE:
   case ACTION_ROOT:
   case ACTION_WIN:
   case ACTION_DESKTOP:
   case ACTION_RDESKTOP:
   case ACTION_LDESKTOP:
   case ACTION_DDESKTOP:
   case ACTION_UDESKTOP:
   case ACTION_SHOWDESK:
   case ACTION_SHOWTRAY:
   case ACTION_EXEC:
   case ACTION_RESTART:
   case ACTION_EXIT:
   case ACTION_FULLSCREEN:
   case ACTION_SENDR:
   case ACTION_SENDL:
   case ACTION_SENDU:
   case ACTION_SENDD:
   case ACTION_MAXTOP:
   case ACTION_MAXBOTTOM:
   case ACTION_MAXLEFT:
   case ACTION_MAXRIGHT:
   case ACTION_MAXV:
   case ACTION_MAXH:
   case ACTION_RESTORE:
      return 1;
   default:
      return 0;
   }
}

/** Get the modifier mask for a key. */
unsigned int GetModifierMask(XModifierKeymap *modmap, KeySym key) {

   KeyCode temp;
   int x;

   temp = JXKeysymToKeycode(display, key);
   if(JUNLIKELY(temp == 0)) {
      Warning(_("Specified KeySym is not defined for any KeyCode"));
   }
   for(x = 0; x < 8 * modmap->max_keypermod; x++) {
      if(modmap->modifiermap[x] == temp) {
         return 1 << (x / modmap->max_keypermod);
      }
   }

   Warning(_("modifier not found for keysym 0x%0x"), key);

   return 0;

}

/** Parse a modifier mask string. */
unsigned int ParseModifierString(const char *str)
{
   unsigned int mask;
   unsigned int x, y;
   char found;

   if(!str) {
      return MASK_NONE;
   }

   mask = MASK_NONE;
   for(x = 0; str[x]; x++) {

      found = 0;
      for(y = 0; modifiers[y].name; y++) {
         if(modifiers[y].name == str[x]) {
            mask |= modifiers[y].mask;
            found = 1;
            break;
         }
      }

      if(JUNLIKELY(!found)) {
         Warning(_("invalid modifier: \"%c\""), str[x]);
      }

   }

   return mask;

}

/** Parse a key string. */
KeySym ParseKeyString(const char *str)
{
   KeySym symbol;
   symbol = JXStringToKeysym(str);
   if(JUNLIKELY(symbol == NoSymbol)) {
      Warning(_("invalid key symbol: \"%s\""), str);
   }
   return symbol;
}

/** Insert a key binding. */
void InsertBinding(ActionType key, const char *modifiers,
                   const char *stroke, const char *code,
                   const char *command)
{

   KeyNode *np;
   unsigned int mask;
   char *temp;
   KeySym sym;

   mask = ParseModifierString(modifiers);

   if(stroke && strlen(stroke) > 0) {
      int offset;

      for(offset = 0; stroke[offset]; offset++) {
         if(stroke[offset] == '#') {

            temp = CopyString(stroke);

            for(temp[offset] = '1'; temp[offset] <= '9'; temp[offset]++) {

               sym = ParseKeyString(temp);
               if(sym == NoSymbol) {
                  Release(temp);
                  return;
               }

               np = Allocate(sizeof(KeyNode));
               np->next = bindings;
               bindings = np;

               np->key = key | ((temp[offset] - '1' + 1) << 8);
               np->state = mask;
               np->symbol = sym;
               np->command = NULL;
               np->code = 0;

            }

            Release(temp);

            return;
         }
      }

      sym = ParseKeyString(stroke);
      if(sym == NoSymbol) {
         return;
      }

      np = Allocate(sizeof(KeyNode));
      np->next = bindings;
      bindings = np;

      np->key = key;
      np->state = mask;
      np->symbol = sym;
      np->command = CopyString(command);
      np->code = 0;

   } else if(code && strlen(code) > 0) {

      np = Allocate(sizeof(KeyNode));
      np->next = bindings;
      bindings = np;

      np->key = key;
      np->state = mask;
      np->symbol = NoSymbol;
      np->command = CopyString(command);
      np->code = atoi(code);

   } else {

      Warning(_("neither key nor keycode specified for Key"));
      np = NULL;

   }
}

/** Insert a mouse binding. */
void InsertMouseBinding(
   unsigned button,
   const char *mask,
   MouseContextType context,
   ActionType key,
   const char *command)
{
   KeyNode *np = Allocate(sizeof(KeyNode));
   np->next = bindings;
   bindings = np;

   np->command = CopyString(command);
   np->key = key;
   np->state = ParseModifierString(mask);
   np->code = button;
   np->context = context;
}

/** Validate key bindings. */
void ValidateKeys(void)
{
   KeyNode *kp;
   for(kp = bindings; kp; kp = kp->next) {
      if((kp->key & 0xFF) == ACTION_ROOT && kp->command) {
         const int bindex = GetRootMenuIndexFromString(kp->command);
         if(JUNLIKELY(!IsRootMenuDefined(bindex))) {
            Warning(_("key binding: root menu \"%s\" not defined"),
                    kp->command);
         }
      }
   }
}

