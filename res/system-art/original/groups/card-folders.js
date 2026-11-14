/* Original static artwork for the card's own folders. */
(() => {
  const assets = [
    {
      "key": "ROMS",
      "name": "ROMs",
      "file": "ROMS-game.png",
      "width": 1254,
      "height": 1254,
      "description": "Purple game cartridge with illustrated paper label and gold contacts",
      "duration": 1000,
      "poseCount": 1,
      "expected_poses": 1,
      "static": true,
      "kind": "card-folder",
      "motionBounds": [
        0,
        0,
        1254,
        1254
      ]
    },
    {
      "key": "BOOKS",
      "name": "Audiobooks",
      "file": "BOOKS.png",
      "width": 1254,
      "height": 1254,
      "description": "Book with headphones",
      "duration": 1000,
      "poseCount": 1,
      "expected_poses": 1,
      "static": true,
      "kind": "card-folder",
      "motionBounds": [
        0,
        0,
        1254,
        1254
      ]
    },
    {
      "key": "VIDEOS",
      "name": "Videos",
      "file": "VIDEOS.png",
      "width": 1254,
      "height": 1254,
      "description": "Generic video tape",
      "duration": 1000,
      "poseCount": 1,
      "expected_poses": 1,
      "static": true,
      "kind": "card-folder",
      "motionBounds": [
        0,
        0,
        1254,
        1254
      ]
    },
    {
      "key": "BIOS",
      "name": "BIOS",
      "file": "BIOS.png",
      "width": 1254,
      "height": 1254,
      "description": "Generic integrated circuit chip",
      "duration": 1000,
      "poseCount": 1,
      "expected_poses": 1,
      "static": true,
      "kind": "card-folder",
      "motionBounds": [
        0,
        0,
        1254,
        1254
      ]
    },
    {
      "key": "SAVES",
      "name": "Saves",
      "file": "SAVES.png",
      "width": 1254,
      "height": 1254,
      "description": "Generic memory card",
      "duration": 1000,
      "poseCount": 1,
      "expected_poses": 1,
      "static": true,
      "kind": "card-folder",
      "motionBounds": [
        0,
        0,
        1254,
        1254
      ]
    },
    {
      "key": "STATES",
      "name": "Save States",
      "file": "STATES.png",
      "width": 1254,
      "height": 1254,
      "description": "Generic floppy disk",
      "duration": 1000,
      "poseCount": 1,
      "expected_poses": 1,
      "static": true,
      "kind": "card-folder",
      "motionBounds": [
        0,
        0,
        1254,
        1254
      ]
    },
    {
      "key": "BGM",
      "name": "Menu music",
      "file": "BGM.png",
      "width": 1254,
      "height": 1254,
      "description": "Generic audio cassette",
      "duration": 1000,
      "poseCount": 1,
      "expected_poses": 1,
      "static": true,
      "kind": "card-folder",
      "motionBounds": [
        0,
        0,
        1254,
        1254
      ]
    },
    {
      "key": "MUSIC",
      "name": "Music",
      "file": "MUSIC.png",
      "width": 1254,
      "height": 1254,
      "description": "Vinyl record with album sleeve",
      "duration": 1000,
      "poseCount": 1,
      "expected_poses": 1,
      "static": true,
      "kind": "card-folder",
      "motionBounds": [
        0,
        0,
        1254,
        1254
      ]
    }
  ];
  window.ZyperOriginalGroups ||= [];
  window.ZyperOriginalGroups.push({
    id: 'card-folders',
    assets,
    extraImages: [],
    render(ctx, key, time, env) {},
    getScreen(key, time, env) { return null; }
  });
})();

