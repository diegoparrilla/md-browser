# SidecarTridge Multi-device File & Download Manager

This microfirmware app for the **SidecarTridge Multi-device** enables you to browse, search, and download files from the public floppy image database, as well as manage files on your microSD card—all from a modern web interface.

## 🚀 Installation

To install the File & Download Manager app on your SidecarTridge Multi-device:

1. **Launch the Booster App** on your SidecarTridge Multi-device.
2. Open the **Booster web interface** in your browser.
3. Go to the **Apps** tab and select **File & Download Manager** from the available apps list.
4. Click **Download** to install the app to your SidecarTridge’s microSD card.
5. Once installed, select the app and click **Launch** to start it.

> **⚠️ WARNING:** The Booster Loader is currently in **alpha**. Use at your own risk.

After launching, the File & Download Manager app will run automatically every time your Atari is powered on.

## 🕹️ Usage

When you start your Atari, the app displays a screen with a **QR code**. Scan this QR code with your smartphone or tablet to access the web interface, or enter the provided URL in any browser.

* To **bypass** the QR code screen and boot directly into GEMDOS, press any key.
* To **return to the Booster App** and select another app, press the **ESC** key.

### 💾 Floppy Images Database

Click the **Floppy DB** menu option in the web interface to access the **Floppy Images Database**. Here you can browse, search, filter, and download floppy images directly to your microSD card.

![Floppy Images Database Search Page](/BROWSER-FLOPPYDB-MAIN.png)

**Search Options:**

* **What's new:** Show only files recently added to the database.
* **Filter by label:** Narrow results by label, such as cracking team names or software type.

Start typing in the search box for smart suggestions. For example, typing `xe` will suggest titles like `Xevious` or `Xenon`.

![Floppy Images Database Search Results](/BROWSER-FLOPPYDB-RESULT.png)

**Search results** appear as a table with:

* **Name**: File name
* **Date**: Date added
* **Label**: Associated label
* **Save to**: Button to save the file to your microSD

To download:

1. Click **Save to** next to the file you want.
2. Choose a destination folder from the dialog.
3. Click **Download Here** to start the download.

> **Note:** You must select a folder and then click **Download Here**; clicking the folder name alone is not enough.

When complete, your file is saved to the chosen folder. Use the [Drives Emulator](https://docs.sidecartridge.com/sidecartridge-multidevice/microfirmwares/drives_emulator/) or a solution like Gotek to access your files on your Atari.

### 🗂️ File Manager

#### Overview

The **File Manager** lets you manage files and folders on your microSD card. You can:

* Navigate folders
* View file details
* Delete files/folders
* Upload multiple files
* Download files
* Create folders
* Upload files from internet URLs

#### Table View

Click **File Manager** in the web interface. You’ll see your microSD card’s contents in a table with columns for:

* **Name**
* **Size**
* **Timestamp**
* **Rename** (icon)
* **Delete** (icon)

Toolbar options above the table:

* **Upload files**: Select files from your computer to upload (multi-select supported)
* **Upload from URL**: Download files directly from an internet URL to the current folder
* **New Folder**: Create a new directory

![File Manager Table View](/BROWSER-FILEMANAGER-ROOT.png)

#### Row Colors

Rows are color-coded:

* **Black:** Standard files and folders
* **Gray:** Hidden files or folders
* **Red:** Read-only files

#### Navigation

* Click a folder row to enter it and see its contents.
* Click **..** at the top to go back to the parent folder.

![File Manager Navigation](/BROWSER-FILEMANAGER-NAVIGATION.png)

#### File Actions

Rename or delete files/folders using the icons. Click a file row for more details and options:

* **Toggle hidden** attribute
* **Toggle read-only** attribute
* **Rename** file
* **Delete** file (with confirmation)
* **Download** file to your computer

![File Details Actions](/BROWSER-FILEMANAGER-FILEACTIONS.png)

#### Uploading Files

Click **Upload files** to select and upload files from your computer (multiple at once). The upload dialog displays progress bars for each file. New files appear automatically in the table once uploaded.

![File Upload Dialog](/BROWSER-FILEMANAGER-UPLOADFILES.png)
![File Upload Done](/BROWSER-FILEMANAGER-UPLOADEDFILES.png)

### 🔌 USB Mass Storage Mode

You can also use your SidecarTridge Multi-device as a USB mass storage device. Simply connect it to your computer via USB, and the microSD card will mount as a regular USB drive. The QR code screen will show a message indicating USB Mass Storage Mode is active.

> **Note:** You can use the web interface and USB mode simultaneously, but changes made in one mode may not show up immediately in the other. Refresh your browser or file explorer to see updates.

### ⏏️ Exiting to GEMDOS or Booster

* **ESC** on the QR code screen launches the **Booster** app.
* **Any other key** exits the emulator and boots into the Atari GEM desktop. The web interface remains active.
* To return to setup, press **SELECT** on your Multi-device and reboot, or power cycle your Atari.
* **Hold SELECT for 10 seconds** to reset your Multi-device to factory settings (useful for troubleshooting or starting fresh).

### ♻️ System Reset Behavior

The Drives Emulator app is **resistant to system resets**. Pressing the Atari reset button will **not interrupt downloads or file management**; your session continues seamlessly.

### 🔄 Power Cycling

After a power cycle, the app automatically returns to the QR code screen, ready for the next session.

### ⚙️ Advanced Features

#### Changing the microSD Card Speed

The SPI bus speed (1 MHz – 24 MHz; default is 12.5 MHz) affects all microSD access and is configured in the Booster App:

1. Launch the **Booster App** (press **X** if you’re in another app).
2. Open the web interface and go to the **Config** tab.
3. Find **SD card bus speed (KHz)** and set your desired speed (e.g., `24000` for 24 MHz).
4. Click **Save** to apply.
5. Relaunch the **Drives Emulator** app.

> **Note:** Values below 1 MHz will default to 1 MHz; above 24 MHz will default to 24 MHz.
>
> **Tip:** 24 MHz is generally safe, but lower the speed if you encounter issues (e.g., 12.5 MHz or 6 MHz). Most modern microSD cards handle these speeds well.

## 📜 License

The source code is licensed under the [GNU General Public License v3.0](LICENSE).

## 🤝 Contributing
Made with ❤️ by [SidecarTridge](https://sidecartridge.com)