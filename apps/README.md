# CrossPoint Apps

This folder contains apps for the CrossPoint e-ink reader. Apps are loaded from the SD card at runtime and appear as menu items on the home screen.

## Installation

Copy the entire `apps` folder to the **root** of your SD card:

```
SD Card Root/
├── apps/
│   ├── rosary/
│   │   └── app.json
│   ├── sample-prayers/
│   │   ├── app.json
│   │   ├── st-francis.txt
│   │   └── serenity.txt
│   ├── bible-random-quotes/
│   │   ├── app.json
│   │   └── quotes.txt
│   └── (your custom apps here)
├── (your ebooks)
└── ...
```

## Creating Your Own App

Each app is a folder inside `/apps/` on the SD card. Every app folder must contain an `app.json` manifest file.

### Text Viewer App

The `textviewer` type displays a list of text files with a simple reading interface. This is the easiest way to create a custom app.

Create a folder under `/apps/` with this structure:

```
apps/my-app/
├── app.json
├── chapter1.txt
├── chapter2.txt
└── chapter3.txt
```

The `app.json` manifest:

```json
{
  "name": "My Custom App",
  "type": "textviewer",
  "version": "1.0",
  "entries": [
    {"title": "Chapter 1", "file": "chapter1.txt"},
    {"title": "Chapter 2", "file": "chapter2.txt"},
    {"title": "Chapter 3", "file": "chapter3.txt"}
  ]
}
```

**Fields:**
- `name` (required): Display name shown in the home menu
- `type` (required): Must be `"textviewer"` for text viewer apps
- `version` (optional): Version string for your reference
- `entries` (required): Array of text entries to display
  - `title`: Menu label for this entry
  - `file`: Text filename relative to the app folder

**Text files** should be plain `.txt` files encoded in UTF-8. Long text will automatically word-wrap and support page-by-page navigation.

### Random Quote App

The `randomquote` type displays one random quote in full-screen mode. This works well with a single large text file.

Manifest example:

```json
{
  "name": "Bible Random Quotes",
  "type": "randomquote",
  "version": "1.0",
  "entries": [
    {"title": "Bible", "file": "quotes.txt"}
  ]
}
```

`quotes.txt` format:
- One quote per line
- Optional `Reference|Quote text` format per line
- Empty lines and lines starting with `#` are ignored

### Book Highlights App (Readwise CSV)

The `bookhighlights` type streams random highlights from CSV files inside `/book-highlights` on the SD card.

Manifest example:

```json
{
  "name": "Book Highlights",
  "type": "bookhighlights",
  "version": "1.0"
}
```

CSV requirements:
- Put one or more `.csv` files in `/book-highlights`
- The parser reads the first three columns as: `Highlight`, `Book Title`, `Book Author`
- Quoted CSV fields are supported, including commas/newlines inside quotes
- Large CSV files are streamed record-by-record (the whole file is not loaded into memory)

Controls:
- `Confirm` or `Down/Right`: load a new random highlight
- `Up/Left`: show the previous highlight from this session
- `Back`: return home

### Built-in App Types

Some app types have specialized UI built into the firmware:

- `rosary` - Holy Rosary prayer guide with bead visualization and decade tracking
- `randomquote` - Full-screen random quote viewer
- `bookhighlights` - Random Readwise CSV highlight viewer from `/book-highlights`

These types only require a minimal `app.json` manifest to activate them.

## Notes

- Apps are discovered each time the home screen loads
- App names are sorted alphabetically in the menu
- The `apps` folder is **not** compiled into the firmware — it is loaded at runtime from the SD card
- Text files are limited to ~4KB per entry due to device memory constraints
