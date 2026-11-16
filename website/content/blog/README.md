# Writing for the Developer Blog

Every post on the Developer Blog (`/blog/`) is a Markdown file in this folder. The site's build turns each one into its own page and lists it on the blog index, newest first. To publish a post, add its folder, commit it, and push to `main`. GitHub Actions builds and deploys the site as usual.

## Add a post

1. Create a folder in `content/blog/` named after the post's address, in lower-case letters, numbers and hyphens. The folder `content/blog/nebula-v2/` becomes `https://nebula.keepvaibin.com/blog/nebula-v2/`. Keep the name once a post is published, so links to it keep working.
2. In the folder, create `index.md` with front matter and the post's text:

   ```markdown
   ---
   title: My new post
   date: 2026-11-20
   summary: One or two sentences for the blog index, search results and link previews.
   author: keepvaibin
   ---

   The first paragraph of the post.
   ```

3. Put the post's images and videos in the same folder, and refer to them by file name.
4. Preview the site with `npm run dev`, and open `/blog/`.
5. Commit the folder and push.

```text
content/blog/
  nebula-v2/
    index.md
  my-new-post/
    index.md
    screenshot.png
    trailer.mp4
```

### Front matter

| Field     | Required | Description                                                                            |
| --------- | -------- | -------------------------------------------------------------------------------------- |
| `title`   | Yes      | The post's title. Put it in quotes if it contains a colon.                             |
| `date`    | Yes      | The publication date, written `YYYY-MM-DD`. Posts are listed newest first.             |
| `summary` | Yes      | One or two sentences. Shown on the blog index, under the title, and in link previews.  |
| `author`  | No       | Shown beside the date.                                                                 |
| `image`   | No       | A file in the post's folder to use as the link preview image, for example `cover.png`. |
| `draft`   | No       | `true` keeps the post off the site. Run `npm run blog:drafts` to preview drafts.       |

Do not start the post with a `#` heading: the title comes from the front matter.

The build stops with a message if a post is missing a field, links to a page or post that does not exist, or uses a file that is not in its folder.

## Syntax

`sample-post/index.md` is a draft that uses everything below. Copy from it, or from this list.

### Text

```markdown
**bold**, _italics_, ~~strikethrough~~ and `inline code`

## A section heading

### A smaller heading

#### The smallest heading

- A bulleted item

1. A numbered item

> A blockquote.
```

### Text size

Headings set the size of section titles. For a phrase in a paragraph, use one of these:

```markdown
<span class="text-small">smaller text</span>
<span class="text-large">larger text</span>
<span class="text-xlarge">much larger text</span>
<small>a small note</small>
```

The post uses the site's fonts throughout. Other HTML is shown as plain text.

### Links

Any word or phrase can be a link:

| To                         | Write                                              |
| -------------------------- | -------------------------------------------------- |
| Another post               | `[read the v2 post](../nebula-v2/)`                |
| A page on the site         | `[mods guide](/play/mods/)`                        |
| A page in the modding docs | `[quickstart](/modding/docs/get-started/quickstart/)`      |
| A heading in the same post | `[see the tables](#tables)`                        |
| Another website            | `[Petari](https://github.com/SMGCommunity/Petari)` |

Write site pages from the site root, starting with `/`. The build turns them into addresses that work on GitHub Pages and checks that the page exists. A heading's anchor is its text in lower case, with punctuation removed and spaces replaced by hyphens.

### Code

````markdown
```lumascript
hook before Mario::tryJump() {
    log "jump"
}
```
````

`lumascript`, `c`, `json` and `shell` are colored. Any other language name, or none, shows plain text.

### Tables

```markdown
| Setting | Default |
| ------- | ------- |
| Bloom   | 100%    |
```

A wide table scrolls sideways inside the post on small screens.

### Images

Put an image on its own line to place it between paragraphs:

```markdown
![Describe the image for people who cannot see it](screenshot.png)
![Describe the image](screenshot.png 'A caption shown under the image')
![Describe the image](https://example.com/picture.jpg)
```

- The text in square brackets is the alt text. It is required.
- The quoted text after the address is an optional caption.
- Supported files: `.png`, `.jpg`, `.jpeg`, `.gif`, `.webp`, `.avif` and `.svg`.
- Images keep their shape and shrink to fit the screen. Save screenshots at the size you want them shown, at most about 2000 pixels wide, to keep the page fast.

### Videos

Videos use the same syntax as images. The address decides what is shown:

```markdown
![Describe the video](gameplay.mp4 'An optional caption')
![Describe the video](https://example.com/clip.mp4)
![Describe the video](https://www.youtube.com/watch?v=VIDEO-ID)
![Describe the video](https://vimeo.com/123456789)
```

| Address                                                                                            | Shown as                                                                              |
| -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------- |
| A `.mp4`, `.m4v`, `.webm` or `.ogv` file in the post's folder                                      | A video player                                                                        |
| A direct link to a `.mp4`, `.m4v`, `.webm` or `.ogv` file on another site                          | A video player                                                                        |
| A YouTube link: `youtube.com/watch?v=`, `youtu.be/`, `youtube.com/shorts/` or `youtube.com/embed/` | The YouTube player, using YouTube's privacy-enhanced mode. A `t=` start time is kept. |
| A Vimeo link: `vimeo.com/NUMBER`                                                                   | The Vimeo player                                                                      |

For video files, MP4 with H.264 video and AAC audio plays in every browser. GitHub refuses files over 100 MB, so keep committed videos small, or upload long videos to YouTube.
