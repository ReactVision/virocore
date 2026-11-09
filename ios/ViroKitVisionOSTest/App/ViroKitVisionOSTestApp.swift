import SwiftUI

@main
struct ViroKitVisionOSTestApp: App {
    @State private var immersionStyle: ImmersionStyle = .mixed

    var body: some Scene {
        WindowGroup {
            ContentView()
                .viroImmersiveSpaceController()
        }
        // A small window, so it does not hide the test scene behind it.
        .defaultSize(width: 520, height: 260)
        .windowResizability(.contentSize)

        ImmersiveSpace(id: ViroImmersiveSpace.id) {
            ViroImmersiveSpaceView()
        }
        .immersionStyle(selection: $immersionStyle, in: .mixed, .full)
    }
}
