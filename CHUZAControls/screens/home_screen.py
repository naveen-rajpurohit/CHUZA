"""The title/connect-status/PLAY screen shown right after launch."""
from PIL import Image, ImageTk

from pixelui import BaseScreen, PixelButton
from theme import ACCENT, CRITICAL, WARNING, make_font, resource_path

_STATUS_COLORS = {
    "online": ACCENT,
    "offline": CRITICAL,
    "connecting": WARNING,
}
_STATUS_TEXT = {
    "online": "ONLINE",
    "offline": "OFFLINE",
    "connecting": "CONNECTING...",
}


class HomeScreen(BaseScreen):
    WIDTH, HEIGHT = 840, 680

    def __init__(self, parent, app):
        super().__init__(parent, app, self.WIDTH, self.HEIGHT)

        logo_img = Image.open(resource_path("assets", "logo.png"))
        self._logo_photo = ImageTk.PhotoImage(logo_img)
        self.canvas.create_image(self.width // 2, 150, image=self._logo_photo, anchor="center")

        self._status_item = self.canvas.create_text(
            self.width // 2, 300, text="", font=make_font(18, bold=True), anchor="center",
        )
        self.set_status("connecting")

        self.play_btn = PixelButton(
            self.canvas, "PLAY", command=app.show_control, width=280, height=76,
            font_size=20, enabled=False,
        )
        self.place_widget(self.play_btn, self.width // 2, 400, anchor="n")

    def set_status(self, status):
        color = _STATUS_COLORS.get(status, ACCENT)
        text = _STATUS_TEXT.get(status, status.upper())
        self.canvas.itemconfigure(self._status_item, text=f"● {text}", fill=color)

    def set_play_enabled(self, enabled):
        self.play_btn.set_enabled(enabled)
