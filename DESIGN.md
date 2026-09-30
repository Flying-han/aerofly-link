---
name: Aerofly Link
description: A glanceable flight-operations console for Aerofly FS 4 community flying.
colors:
  primary: "#4ECFE1"
  primary-light: "#176980"
  neutral-bg: "#0B121A"
  neutral-surface: "#131E28"
  neutral-raised: "#1B2A36"
  neutral-border: "#304350"
  neutral-text: "#E8F0F4"
  neutral-muted: "#A4B5C0"
  state-success: "#5DD3A0"
  state-warning: "#FFBA5C"
  state-danger: "#EE7077"
  light-bg: "#E7EEF1"
  light-surface: "#F7FAFB"
  light-raised: "#D7E3E8"
  light-border: "#B4C4CB"
  light-text: "#1B2931"
  light-muted: "#4B5E67"
  light-success: "#1B704B"
  light-warning: "#914B0A"
  light-danger: "#A62F38"
  ink: "#0A161D"
typography:
  display:
    fontFamily: "Segoe UI"
    fontSize: "21px"
    fontWeight: 400
  body:
    fontFamily: "Segoe UI"
    fontSize: "15px"
    fontWeight: 400
  label:
    fontFamily: "Segoe UI"
    fontSize: "12px"
    fontWeight: 400
  measurement:
    fontFamily: "Consolas"
    fontSize: "27px"
    fontWeight: 400
rounded:
  input: "4px"
  control: "5px"
  panel: "7px"
spacing:
  xs: "8px"
  sm: "10px"
  md: "12px"
  lg: "14px"
components:
  button-primary:
    backgroundColor: "{colors.primary}"
    textColor: "{colors.ink}"
    rounded: "{rounded.control}"
    padding: "10px 7px"
  button-secondary:
    backgroundColor: "{colors.neutral-raised}"
    textColor: "{colors.neutral-text}"
    rounded: "{rounded.control}"
    padding: "10px 7px"
  button-alert:
    backgroundColor: "{colors.state-danger}"
    textColor: "{colors.ink}"
    rounded: "{rounded.control}"
    padding: "10px 7px"
  input:
    backgroundColor: "{colors.neutral-surface}"
    textColor: "{colors.neutral-text}"
    rounded: "{rounded.input}"
    padding: "8px 6px"
  navigation-active:
    backgroundColor: "{colors.neutral-raised}"
    textColor: "{colors.primary}"
    rounded: "{rounded.control}"
    padding: "10px 7px"
---

# Design System: Aerofly Link

## Overview

**Creative North Star: “Flight Operations Console”**

The interface is a desktop flight-operations workspace: pilots set up a community connection, then scan server state, transponder mode, telemetry, flight plan, and messages. The user chose a dark flight context. The default palette uses deep blue-charcoal surfaces and a clear cyan action color; a matching light theme is available for brighter rooms.

The layout gives setup a short, readable path and gives connected work a stable place for flight controls and messages. Server choices use full host and port text. Authentication uses FSD-JWT or legacy descriptions. Advanced endpoints and proxy details stay behind an explicit settings disclosure.

**Key Characteristics:**

- Flat dark surfaces with crisp borders and measured cyan accents.
- Status words accompany every state color.
- A single resizable window reflows controls into stacked or side-by-side groups.
- Mainland Simplified Chinese, Hong Kong Traditional Chinese, and American English share the same information order.

## Colors

The dark palette is the default; light-theme semantic colors use darker inks so status and helper text remain legible.

### Primary

- **Flight Cyan** (`{colors.primary}`): The primary connection and save actions, selected navigation, and keyboard focus.
- **Flight Cyan Deep** (`{colors.primary-light}`): The light theme's interactive accent.

### Secondary

- **Ready Green** (`{colors.state-success}`): Connected and healthy telemetry states.
- **Caution Amber** (`{colors.state-warning}`): Missing telemetry, connection progress, and policy notices.
- **Alert Coral** (`{colors.state-danger}`): The 7700 emergency transponder shortcut.

### Neutral

- **Night Flight** (`{colors.neutral-bg}`): Main canvas.
- **Panel Slate** (`{colors.neutral-surface}`): Inputs and nested work areas.
- **Raised Slate** (`{colors.neutral-raised}`): Secondary controls and selected surfaces.
- **Instrument Line** (`{colors.neutral-border}`): One-pixel separation between adjacent regions.
- **Cloud White** (`{colors.neutral-text}`): Primary dark-theme text.
- **Runway Mist** (`{colors.neutral-muted}`): Labels and helper copy.
- **Daylight Canvas** (`{colors.light-bg}`), **Paper Panel** (`{colors.light-surface}`), **Mist Raised** (`{colors.light-raised}`), and **Slate Ink** (`{colors.light-text}`): The light theme's semantic surfaces and text.
- **Deep Ink** (`{colors.ink}`): Text on bright dark-theme primary actions.

### Named Rules

**The Status-in-Words Rule.** State colors always appear with a readable label such as Connected, Standby, or Waiting for telemetry.

## Typography

**Display Font:** Segoe UI
**Body Font:** Segoe UI
**Label/Mono Font:** Segoe UI for labels; Consolas for transponder measurements.

**Character:** Platform-native sans text keeps setup labels familiar. Consolas reserves its fixed-width shape for the transponder code.

### Hierarchy

- **Display** (regular, `{typography.display.fontSize}`): Page headings and the product name.
- **Body** (regular, `{typography.body.fontSize}`): Inputs, actions, and flight-operation values.
- **Label** (regular, `{typography.label.fontSize}`): Field names and supporting text.
- **Measurement** (regular, `{typography.measurement.fontSize}`): The active transponder code.

## Layout

The top row keeps product identity and the three destinations aligned. The connection page uses two columns when the logical window width is at least 850 px and stacks below that. Flight-plan fields use three columns at 900 px; the connected dashboard uses paired operation groups at 1050 px and stacks them below. Groups scroll internally when the window is shorter than their content. The window can be resized and follows monitor DPI changes.

The spacing rhythm uses 8, 10, 12, and 14 logical px steps. Form labels sit above their fields; labels and fields share column baselines.

## Elevation & Depth

The interface does not use shadows. It separates surfaces with small shifts in luminance and one-pixel borders. Dark and light themes use their own semantic text and status colors rather than inverting the same values.

## Shapes

Inputs use 4 px corners, controls 5 px, and the content panel 7 px. Borders stay thin and consistent; bright fills are reserved for the primary action and the emergency transponder shortcut.

## Components

### Buttons

- **Primary:** Flight Cyan fill, high-contrast text, 5 px corners; use for the next decisive action.
- **Secondary:** Raised surface and normal text; use for navigation, saved-server management, and reversible controls.
- **Caution:** Alert Coral is reserved for the 7700 shortcut and remains paired with its numeric label.
- **Focus:** The selected navigation and focused native password edit have a visible outline or accent.

### Inputs / Fields

- **Style:** Surface fill, one-pixel border, 4 px corners, consistent row height.
- **Focus:** Accent outline; the password control uses the native masked edit field.
- **Error:** Show a recovery message in the page and status area; preserve the form values.

### Navigation

Three full-label buttons—Connection, Flight deck, and Settings—stay in the top bar. The selected page uses the accent color and border.

### Containers

Flight status, transponder controls, flight-plan fields, and messages occupy separate bordered groups. Groups do not use decorative shadows or nested icon cards.

## Do's and Don'ts

### Do

- **Do** show a server as a complete `host:port` address.
- **Do** explain authentication and advanced network settings in plain language.
- **Do** keep the default ASC endpoint visible and available in server history.
- **Do** retain keyboard focus and readable state labels at compact widths.

### Don't

- **Don't** display `ECO` or `TYPE` as user-facing labels.
- **Don't** use Sweatbox as a normal community-flight default.
- **Don't** present a VATSIM address as permission for this client to connect.
- **Don't** use a glyph alone for delete, emergency, or connection actions.
